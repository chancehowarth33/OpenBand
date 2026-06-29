/**
 * @file task_ble.c
 * @brief BLE task using WICED BT stack via btstack-integration middleware.
 *
 * Architecture:
 *   - CM0+ runs the BLESS link-layer controller (CM0P_BLESS firmware).
 *   - CM4 (this file) runs the WICED BT host stack (GAP + GATT) via IPC.
 *     The btstack-integration COMPONENT_BLESS-IPC transport is activated
 *     automatically by the BSP (DEVICE_COMPONENTS = BLESS-IPC).
 *   - wiced_bt_stack_init() starts the stack. BTM_ENABLED_EVT fires when ready.
 *
 * GATT services (defined via BT Configurator):
 *   WatchWise Custom Health Service (128-bit UUID)
 *     SpO2  characteristic (handle 0x000A) — notify, 1 byte %
 *     Steps characteristic (handle 0x000D) — notify, 4 bytes LE uint32
 *
 * Phone testing: nRF Connect → scan for "WatchWise" → Connect →
 * enable notifications on SP02 and StepCount in the WatchWise Custom Service.
 */

#include "task_ble.h"
#include "task_system_control.h"
#include "health_algo.h"

/* WICED BT stack headers (btstack library, pulled by btstack-integration) */
#include "wiced_bt_dev.h"
#include "wiced_bt_stack.h"
#include "wiced_bt_gatt.h"
#include "wiced_bt_ble.h"
#include "wiced_bt_cfg.h"
#include "wiced_bt_types.h"
#ifdef LOW_POWER
#include "wiced_bt_l2c.h"          /* wiced_bt_l2cap_update_ble_conn_params */
#endif

/* BT Configurator generated output */
#include "cycfg_bt_settings.h"   /* wiced_bt_cfg_settings */
#include "cycfg_gatt_db.h"       /* gatt_database[], HDLC_* / HDLD_* handles */
#include "cycfg_gap.h"           /* cy_bt_adv_packet_data[], cy_bt_device_address */

/* BSP BT platform config — IPC channel config for CM0+ BLESS controller */
#include "cybsp_bt_config.h"     /* cybsp_bt_platform_cfg */
#include "cybt_platform_config.h"/* cybt_platform_config_init() */
#include "wiced_memory.h"        /* wiced_bt_get_buffer / wiced_bt_free_buffer */
#include "gattdefs.h"            /* GATT_UUID_CHAR_DECLARE, IS_CHAR_DECLARATION_UUID */

#include <string.h>
#include <stdio.h>

/* Callback type for freeing GATT response buffers */
typedef void (*pfn_free_buffer_t)(uint8_t *);

/* Advertising duty. LOW_POWER builds advertise at the cfg low-duty interval
 * (~1 s) instead of high-duty 30 ms — a big radio saving while disconnected. */
#ifdef LOW_POWER
#define WW_ADV_MODE  BTM_BLE_ADVERT_UNDIRECTED_LOW
#else
#define WW_ADV_MODE  BTM_BLE_ADVERT_UNDIRECTED_HIGH
#endif

/* --------------------------------------------------------------------------
 * Runtime state
 * -------------------------------------------------------------------------- */
volatile bool ble_connected = false;
static uint16_t ble_conn_id = 0;

/* CCCD shadows — 0x0001 = notifications enabled (GATT_CLIENT_CONFIG_NOTIFICATION) */
static uint16_t spo2_cccd  = 0;
static uint16_t steps_cccd = 0;
static uint16_t hr_cccd    = 0;
static uint16_t bat_cccd   = 0;

/* --------------------------------------------------------------------------
 * Forward declaration
 * -------------------------------------------------------------------------- */
static wiced_bt_gatt_status_t app_gatt_callback(wiced_bt_gatt_evt_t event,
                                                  wiced_bt_gatt_event_data_t *p_data);

/* --------------------------------------------------------------------------
 * BT management callback
 * Handles stack lifecycle: BTM_ENABLED_EVT, advert state changes.
 * -------------------------------------------------------------------------- */
static wiced_bt_dev_status_t app_bt_management_callback(
    wiced_bt_management_evt_t        event,
    wiced_bt_management_evt_data_t  *p_event_data)
{
    switch (event)
    {
        case BTM_ENABLED_EVT:
            if (p_event_data->enabled.status == WICED_BT_SUCCESS) {
                wiced_bt_gatt_status_t db_status =
                    wiced_bt_gatt_db_init(gatt_database, gatt_database_len, NULL);
                PRINTF("[BLE] gatt_db_init status=%d db_len=%u\n",
                       (int)db_status, gatt_database_len);
                wiced_bt_gatt_register(app_gatt_callback);
                /* Set advertising payload (includes device name "WatchWise") */
                wiced_bt_ble_set_raw_advertisement_data(CY_BT_ADV_PACKET_ELEM_COUNT,
                                                        cy_bt_adv_packet_data);
                wiced_bt_start_advertisements(WW_ADV_MODE, 0, NULL);
                PRINTF("[BLE] Stack ready — advertising as \"WatchWise2\"\n");
            } else {
                PRINTF("[BLE] Stack init FAILED: %d\n",
                       (int)p_event_data->enabled.status);
            }
            break;

        case BTM_DISABLED_EVT:
            PRINTF("[BLE] Stack disabled\n");
            break;

        case BTM_BLE_ADVERT_STATE_CHANGED_EVT:
            /* Log only. Re-arming on timeout is handled in the task loop via
             * wiced_bt_ble_get_current_advert_mode(), which avoids a race: on
             * connect this event's ADVERT_OFF fires BEFORE GATT_CONNECTION_STATUS
             * sets ble_connected, so restarting advertising here would kick off
             * a fresh advertisement mid-handshake and disrupt the connection. */
            PRINTF("[BLE] Advert state: %d\n",
                   (int)p_event_data->ble_advert_state_changed);
            break;

        default:
            break;
    }
    return WICED_BT_SUCCESS;
}

/* --------------------------------------------------------------------------
 * Lookup a handle in the BT Configurator ext-attr table.
 * Returns pointer to entry, or NULL if not found.
 * -------------------------------------------------------------------------- */
static gatt_db_lookup_table_t *app_find_attr(uint16_t handle)
{
    for (uint16_t i = 0; i < app_gatt_db_ext_attr_tbl_size; i++) {
        if (app_gatt_db_ext_attr_tbl[i].handle == handle) {
            return &app_gatt_db_ext_attr_tbl[i];
        }
    }
    return NULL;
}

/* --------------------------------------------------------------------------
 * GATT write handler
 * Intercepts CCCD writes to track when phone enables notifications.
 * -------------------------------------------------------------------------- */
static wiced_bt_gatt_status_t app_gatt_write_handler(
    uint16_t conn_id, wiced_bt_gatt_opcode_t opcode,
    wiced_bt_gatt_write_req_t *p_write)
{
    uint16_t handle = p_write->handle;

    /* Try to find the attribute in the lookup table and update its backing store */
    gatt_db_lookup_table_t *attr = app_find_attr(handle);
    if (attr == NULL) {
        return WICED_BT_GATT_WRITE_NOT_PERMIT;
    }

    uint16_t write_len = p_write->val_len;
    if (write_len > attr->max_len) {
        write_len = attr->max_len;
    }
    memcpy(attr->p_data, p_write->p_val, write_len);
    attr->cur_len = write_len;

    /* Mirror CCCDs into our shadow variables for the notification loop */
    if (handle == HDLD_WATCHWISE_HEALTH_SP02_CLIENT_CHAR_CONFIG) {
        spo2_cccd = attr->p_data[0] | ((uint16_t)attr->p_data[1] << 8);
        PRINTF("[BLE] SpO2 notifications %s\n",
               (spo2_cccd & GATT_CLIENT_CONFIG_NOTIFICATION) ? "ON" : "OFF");
    } else if (handle == HDLD_WATCHWISE_HEALTH_STEPCOUNT_CLIENT_CHAR_CONFIG) {
        steps_cccd = attr->p_data[0] | ((uint16_t)attr->p_data[1] << 8);
        PRINTF("[BLE] Steps notifications %s\n",
               (steps_cccd & GATT_CLIENT_CONFIG_NOTIFICATION) ? "ON" : "OFF");
    } else if (handle == HDLD_HRS_HEART_RATE_MEASUREMENT_CLIENT_CHAR_CONFIG) {
        hr_cccd = attr->p_data[0] | ((uint16_t)attr->p_data[1] << 8);
        PRINTF("[BLE] HR notifications %s\n",
               (hr_cccd & GATT_CLIENT_CONFIG_NOTIFICATION) ? "ON" : "OFF");
    } else if (handle == HDLD_BAS_BATTERY_LEVEL_CLIENT_CHAR_CONFIG) {
        bat_cccd = attr->p_data[0] | ((uint16_t)attr->p_data[1] << 8);
        PRINTF("[BLE] Battery notifications %s\n",
               (bat_cccd & GATT_CLIENT_CONFIG_NOTIFICATION) ? "ON" : "OFF");
    }

    /* Acknowledge GATT_REQ_WRITE; no ack needed for GATT_CMD_WRITE */
    if (opcode == GATT_REQ_WRITE) {
        wiced_bt_gatt_server_send_write_rsp(conn_id, opcode, handle);
    }
    return WICED_BT_GATT_SUCCESS;
}

/* --------------------------------------------------------------------------
 * GATT read handler
 * Uses the BT Configurator lookup table to handle ALL attribute reads.
 * Live health values are written into the backing arrays before lookup.
 * -------------------------------------------------------------------------- */
static wiced_bt_gatt_status_t app_gatt_read_handler(
    uint16_t conn_id, wiced_bt_gatt_opcode_t opcode,
    wiced_bt_gatt_read_t *p_read)
{
    uint16_t handle = p_read->handle;

    /* Update dynamic backing arrays with live sensor data before lookup */
    if (handle == HDLC_WATCHWISE_HEALTH_SP02_VALUE) {
        app_watchwise_health_sp02[0] = health_spo2;
    } else if (handle == HDLC_WATCHWISE_HEALTH_STEPCOUNT_VALUE) {
        uint32_t steps = health_steps;
        memcpy(app_watchwise_health_stepcount, &steps, sizeof(steps));
    } else if (handle == HDLC_HRS_HEART_RATE_MEASUREMENT_VALUE) {
        /* flags=0x00 (8-bit HR format), then BPM */
        app_hrs_heart_rate_measurement[0] = 0x00u;
        app_hrs_heart_rate_measurement[1] = health_hr;
    } else if (handle == HDLC_BAS_BATTERY_LEVEL_VALUE) {
        app_bas_battery_level[0] = health_battery;
    }

    /* Generic lookup — handles Device Name, Appearance, CCCDs, everything */
    gatt_db_lookup_table_t *attr = app_find_attr(handle);
    if (attr == NULL) {
        wiced_bt_gatt_server_send_error_rsp(conn_id, opcode, handle,
                                             WICED_BT_GATT_ATTRIBUTE_NOT_FOUND);
        return WICED_BT_GATT_ATTRIBUTE_NOT_FOUND;
    }

    wiced_bt_gatt_server_send_read_handle_rsp(conn_id, opcode,
        attr->cur_len, attr->p_data, NULL);
    return WICED_BT_GATT_SUCCESS;
}

/* --------------------------------------------------------------------------
 * GATT READ_BY_TYPE handler — characteristic discovery (opcode 0x08).
 * In WICED BT v3.x the stack does not handle this internally; the app must
 * walk the registered gatt_database[] and build the ATT response manually.
 * -------------------------------------------------------------------------- */
#define READ_BY_TYPE_RSP_MAX (21u * 20u)  /* 20 chars × 21 bytes (128-bit UUID) */

static wiced_bt_gatt_status_t app_gatt_read_by_type_handler(
    uint16_t conn_id,
    wiced_bt_gatt_opcode_t opcode,
    wiced_bt_gatt_read_by_type_t *p_rbt)
{
    uint16_t s_handle = p_rbt->s_handle;
    uint16_t e_handle = p_rbt->e_handle;

    PRINTF("[GATT] READ_BY_TYPE s=0x%04X e=0x%04X uuid16=0x%04X\n",
           s_handle, e_handle,
           (p_rbt->uuid.len == 2u) ? p_rbt->uuid.uu.uuid16 : 0u);

    /* Only handle characteristic discovery (UUID = 0x2803) */
    if (!IS_CHAR_DECLARATION_UUID(p_rbt->uuid)) {
        wiced_bt_gatt_server_send_error_rsp(conn_id, opcode, s_handle,
                                             WICED_BT_GATT_ATTRIBUTE_NOT_FOUND);
        return WICED_BT_GATT_ATTRIBUTE_NOT_FOUND;
    }

    uint8_t *p_rsp = wiced_bt_get_buffer(READ_BY_TYPE_RSP_MAX);
    if (!p_rsp) {
        wiced_bt_gatt_server_send_error_rsp(conn_id, opcode, s_handle,
                                             WICED_BT_GATT_INSUF_RESOURCE);
        return WICED_BT_GATT_INSUF_RESOURCE;
    }

    uint8_t  *p       = p_rsp;
    uint16_t  total   = 0u;
    uint8_t   pair_len = 0u;

    const wiced_gattdb_entry_t     *p_entry = NULL;
    wiced_bt_gatt_discovery_data_t  disc;

    do {
        p_entry = wiced_bt_gattdb_local_read_data_by_type(
                      GATT_DISCOVER_CHARACTERISTICS, p_entry, &disc);
        if (!p_entry) break;

        wiced_bt_gatt_char_declaration_t *ch = &disc.characteristic_declaration;

        if (ch->handle < s_handle) continue;   /* before range */
        if (ch->handle > e_handle) break;      /* past range — stop */

        uint8_t uuid_len      = ch->char_uuid.len;
        uint8_t this_pair_len = 2u + 1u + 2u + uuid_len; /* decl_handle + props + val_handle + uuid */

        if (pair_len == 0u) {
            pair_len = this_pair_len;
        } else if (this_pair_len != pair_len) {
            /* UUID width changed — client must send a new request for the rest */
            break;
        }

        if (total + pair_len > READ_BY_TYPE_RSP_MAX) break;

        p[0] = (uint8_t)(ch->handle);
        p[1] = (uint8_t)(ch->handle >> 8u);
        p[2] = ch->characteristic_properties;
        p[3] = (uint8_t)(ch->val_handle);
        p[4] = (uint8_t)(ch->val_handle >> 8u);
        if (uuid_len == 2u) {
            p[5] = (uint8_t)(ch->char_uuid.uu.uuid16);
            p[6] = (uint8_t)(ch->char_uuid.uu.uuid16 >> 8u);
        } else {
            memcpy(&p[5], ch->char_uuid.uu.uuid128, 16u);
        }
        p     += pair_len;
        total += pair_len;

    } while (p_entry);

    if (total == 0u) {
        wiced_bt_free_buffer(p_rsp);
        wiced_bt_gatt_server_send_error_rsp(conn_id, opcode, s_handle,
                                             WICED_BT_GATT_ATTRIBUTE_NOT_FOUND);
        return WICED_BT_GATT_ATTRIBUTE_NOT_FOUND;
    }

    PRINTF("[GATT] READ_BY_TYPE rsp: %u chars, pair_len=%u, total=%u\n",
           total / pair_len, pair_len, total);

    return wiced_bt_gatt_server_send_read_by_type_rsp(conn_id, opcode,
               pair_len, total, p_rsp,
               (wiced_bt_gatt_app_context_t)wiced_bt_free_buffer);
}

/* --------------------------------------------------------------------------
 * Main GATT event callback (registered via wiced_bt_gatt_register)
 * -------------------------------------------------------------------------- */
static wiced_bt_gatt_status_t app_gatt_callback(wiced_bt_gatt_evt_t event,
                                                  wiced_bt_gatt_event_data_t *p_data)
{
    switch (event)
    {
        case GATT_CONNECTION_STATUS_EVT:
            if (p_data->connection_status.connected) {
                ble_conn_id   = p_data->connection_status.conn_id;
                ble_connected = true;
                PRINTF("[BLE] Connected  conn_id=%u\n", ble_conn_id);
#ifdef LOW_POWER
                /* Request a slow, latency-tolerant link: ~400-500 ms interval,
                 * skip 4 events, 6 s supervision. Lets the radio idle between
                 * our ~2 s notifications instead of waking every 100 ms. */
                wiced_bt_l2cap_update_ble_conn_params(
                    p_data->connection_status.bd_addr,
                    320,   /* min interval 400 ms (320 * 1.25 ms) */
                    400,   /* max interval 500 ms */
                    4,     /* slave latency (events skippable) */
                    600);  /* supervision timeout 6 s (600 * 10 ms) */
#endif
            } else {
                ble_connected = false;
                spo2_cccd     = 0;
                steps_cccd    = 0;
                hr_cccd       = 0;
                bat_cccd      = 0;
                PRINTF("[BLE] Disconnected — restarting advertisements\n");
                wiced_bt_start_advertisements(WW_ADV_MODE, 0, NULL);
            }
            break;

        case GATT_ATTRIBUTE_REQUEST_EVT: {
            wiced_bt_gatt_attribute_request_t *req = &p_data->attribute_request;

            if (req->opcode == GATT_REQ_MTU) {
                /* App must reply to MTU exchange — stack does not auto-respond */
                PRINTF("[GATT] MTU exchange: remote=%u, replying with %u\n",
                       req->data.remote_mtu, CY_BT_RX_PDU_SIZE);
                return wiced_bt_gatt_server_send_mtu_rsp(req->conn_id,
                                                          req->data.remote_mtu,
                                                          CY_BT_RX_PDU_SIZE);
            }
            if (req->opcode == GATT_REQ_READ_BY_TYPE) {
                return app_gatt_read_by_type_handler(req->conn_id, req->opcode,
                                                      &req->data.read_by_type);
            }
            if (req->opcode == GATT_REQ_WRITE || req->opcode == GATT_CMD_WRITE) {
                return app_gatt_write_handler(req->conn_id, req->opcode,
                                              &req->data.write_req);
            }
            if (req->opcode == GATT_REQ_READ || req->opcode == GATT_REQ_READ_BLOB) {
                return app_gatt_read_handler(req->conn_id, req->opcode,
                                             &req->data.read_req);
            }
            /* 0x1B = notification transmitted, 0x1E = indication confirmed — no action */
            if (req->opcode == GATT_HANDLE_VALUE_NOTIF ||
                req->opcode == GATT_HANDLE_VALUE_CONF) {
                return WICED_BT_GATT_SUCCESS;
            }
            PRINTF("[GATT] Unhandled opcode=0x%02X\n", (int)req->opcode);
            break;
        }

        case GATT_GET_RESPONSE_BUFFER_EVT:
            /* Stack needs a buffer to transmit a GATT response/notification.
             * Allocate from the WICED BT heap; freed on GATT_APP_BUFFER_TRANSMITTED_EVT. */
            p_data->buffer_request.buffer.p_app_rsp_buffer =
                wiced_bt_get_buffer(p_data->buffer_request.len_requested);
            p_data->buffer_request.buffer.p_app_ctxt =
                (wiced_bt_gatt_app_context_t)wiced_bt_free_buffer;
            break;

        case GATT_APP_BUFFER_TRANSMITTED_EVT: {
            pfn_free_buffer_t pfn_free =
                (pfn_free_buffer_t)p_data->buffer_xmitted.p_app_ctxt;
            if (pfn_free) {
                pfn_free(p_data->buffer_xmitted.p_app_data);
            }
            break;
        }

        default:
            break;
    }
    return WICED_BT_GATT_SUCCESS;
}

/* --------------------------------------------------------------------------
 * Helper: send a BLE notification if phone has subscribed.
 * -------------------------------------------------------------------------- */
static void app_send_notify(uint16_t handle, uint8_t *data, uint16_t len,
                             uint16_t cccd)
{
    if (!ble_connected) return;
    if (!(cccd & GATT_CLIENT_CONFIG_NOTIFICATION)) return;

    wiced_bt_gatt_server_send_notification(ble_conn_id, handle, len, data, NULL);
}

/* --------------------------------------------------------------------------
 * BLE Task
 * -------------------------------------------------------------------------- */

/* WICED BT heap — WICED_DEFAULT_HEAP is NULL by default so wiced_bt_get_buffer()
 * always fails unless we create a heap and mark it as the default first.
 * The stack uses this for every ATT response/notification packet it sends. */
#define BT_HEAP_SIZE 4096
static uint8_t bt_heap_buf[BT_HEAP_SIZE];

void task_ble(void *pvParameters)
{
    PRINTF("[BLE] Task started\n");

    /* Create WICED BT default heap from our static buffer.
     * Must happen before cybt_platform_config_init / wiced_bt_stack_init
     * so the stack can allocate from it during bring-up. */
    wiced_bt_heap_t *p_heap = wiced_bt_create_heap("bt_heap",
                                                     bt_heap_buf,
                                                     BT_HEAP_SIZE,
                                                     NULL,
                                                     WICED_TRUE);
    PRINTF("[BLE] BT heap: %p (%d bytes)\n", (void *)p_heap, BT_HEAP_SIZE);

    /* Configure IPC transport to CM0+ BLESS controller — must come first */
    cybt_platform_config_init(&cybsp_bt_platform_cfg);
    PRINTF("[BLE] Platform config done\n");

    /* Start WICED BT stack. BTM_ENABLED_EVT fires asynchronously when ready. */
    wiced_result_t bt_init_result = wiced_bt_stack_init(app_bt_management_callback,
                                                         &wiced_bt_cfg_settings);
    PRINTF("[BLE] wiced_bt_stack_init returned: %d\n", (int)bt_init_result);

    TickType_t last_notify  = xTaskGetTickCount();
    TickType_t last_alive   = xTaskGetTickCount();
    TickType_t last_adv     = xTaskGetTickCount();

    for (;;) {
        /* Print a heartbeat every 5 s so we know the task loop is running */
        if ((xTaskGetTickCount() - last_alive) >= pdMS_TO_TICKS(5000)) {
            last_alive = xTaskGetTickCount();
            PRINTF("[BLE] loop alive, connected=%d\n", ble_connected);
        }

        /* Re-arm advertising if it has timed out while disconnected. Polling
         * here (rather than in the BTM_BLE_ADVERT_STATE_CHANGED event) reads the
         * real advert state and a settled ble_connected, so it never restarts
         * advertising during a connection handshake. Advertising in cycfg_gap.h
         * is configured for 60s high + 60s low duty, then stops — without this
         * the device disappears from scanners ~2 min after boot/disconnect. */
        if ((xTaskGetTickCount() - last_adv) >= pdMS_TO_TICKS(1000)) {
            last_adv = xTaskGetTickCount();
            if (!ble_connected &&
                wiced_bt_ble_get_current_advert_mode() == BTM_BLE_ADVERT_OFF) {
                wiced_bt_start_advertisements(WW_ADV_MODE,
                                              0, NULL);
                PRINTF("[BLE] Advertising was off — re-armed\n");
            }
        }

        if ((xTaskGetTickCount() - last_notify) >= pdMS_TO_TICKS(2000)) {
            last_notify = xTaskGetTickCount();

            if (ble_connected) {
                /* Heart Rate Measurement: flags=0x00 (8-bit HR), then BPM */
                uint8_t hr_pkt[2] = { 0x00u, health_hr };
                app_send_notify(HDLC_HRS_HEART_RATE_MEASUREMENT_VALUE,
                                hr_pkt, sizeof(hr_pkt), hr_cccd);

                /* SpO2 — 1 byte percent */
                uint8_t spo2 = health_spo2;
                app_send_notify(HDLC_WATCHWISE_HEALTH_SP02_VALUE,
                                &spo2, 1u, spo2_cccd);

                /* Step count — 4-byte little-endian uint32 */
                uint32_t steps = health_steps;
                app_send_notify(HDLC_WATCHWISE_HEALTH_STEPCOUNT_VALUE,
                                (uint8_t *)&steps, (uint16_t)sizeof(steps),
                                steps_cccd);

                /* Battery level — 1 byte percent */
                app_bas_battery_level[0] = health_battery;
                app_send_notify(HDLC_BAS_BATTERY_LEVEL_VALUE,
                                app_bas_battery_level, 1u, bat_cccd);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* --------------------------------------------------------------------------
 * Public init — call once from system_control_initialize()
 * -------------------------------------------------------------------------- */
void ble_task_initialize(void)
{
    /* WICED BT stack needs substantial stack: cybt_platform_config_init +
     * wiced_bt_stack_init each make deep calls into the IPC/BT layer.
     * 4096 words = 16 KB is the safe minimum for this board. */
    xTaskCreate(task_ble, "BLE",
                configMINIMAL_STACK_SIZE * 32, NULL,
                tskIDLE_PRIORITY + 1, NULL);
}

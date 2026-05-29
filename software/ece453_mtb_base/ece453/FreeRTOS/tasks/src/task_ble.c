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
#include "health_algo.h"

/* WICED BT stack headers (btstack library, pulled by btstack-integration) */
#include "wiced_bt_dev.h"
#include "wiced_bt_stack.h"
#include "wiced_bt_gatt.h"
#include "wiced_bt_ble.h"
#include "wiced_bt_cfg.h"
#include "wiced_bt_types.h"

/* BT Configurator generated output */
#include "cycfg_bt_settings.h"   /* wiced_bt_cfg_settings */
#include "cycfg_gatt_db.h"       /* gatt_database[], HDLC_* / HDLD_* handles */
#include "cycfg_gap.h"           /* cy_bt_adv_packet_data[], cy_bt_device_address */

#include <string.h>
#include <stdio.h>

/* --------------------------------------------------------------------------
 * Runtime state
 * -------------------------------------------------------------------------- */
volatile bool ble_connected = false;
static uint16_t ble_conn_id = 0;

/* CCCD shadows — 0x0001 = notifications enabled (GATT_CLIENT_CONFIG_NOTIFICATION) */
static uint16_t spo2_cccd  = 0;
static uint16_t steps_cccd = 0;
static uint16_t hr_cccd    = 0;

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
                wiced_bt_gatt_db_init(gatt_database, gatt_database_len, NULL);
                wiced_bt_gatt_register(app_gatt_callback);
                wiced_bt_start_advertisements(BTM_BLE_ADVERT_UNDIRECTED_HIGH, 0, NULL);
                printf("[BLE] Stack ready — advertising as \"WatchWise\"\n");
            } else {
                printf("[BLE] Stack init FAILED: %d\n",
                       (int)p_event_data->enabled.status);
            }
            break;

        case BTM_DISABLED_EVT:
            printf("[BLE] Stack disabled\n");
            break;

        case BTM_BLE_ADVERT_STATE_CHANGED_EVT:
            printf("[BLE] Advert state: %d\n",
                   (int)p_event_data->ble_advert_state_changed);
            break;

        default:
            break;
    }
    return WICED_BT_SUCCESS;
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

    if (handle == HDLD_WATCHWISE_HEALTH_SP02_CLIENT_CHAR_CONFIG) {
        spo2_cccd = p_write->p_val[0] | ((uint16_t)p_write->p_val[1] << 8);
        printf("[BLE] SpO2 notifications %s\n",
               (spo2_cccd & GATT_CLIENT_CONFIG_NOTIFICATION) ? "ON" : "OFF");
    } else if (handle == HDLD_WATCHWISE_HEALTH_STEPCOUNT_CLIENT_CHAR_CONFIG) {
        steps_cccd = p_write->p_val[0] | ((uint16_t)p_write->p_val[1] << 8);
        printf("[BLE] Steps notifications %s\n",
               (steps_cccd & GATT_CLIENT_CONFIG_NOTIFICATION) ? "ON" : "OFF");
    } else if (handle == HDLD_HRS_HEART_RATE_MEASUREMENT_CLIENT_CHAR_CONFIG) {
        hr_cccd = p_write->p_val[0] | ((uint16_t)p_write->p_val[1] << 8);
        printf("[BLE] HR notifications %s\n",
               (hr_cccd & GATT_CLIENT_CONFIG_NOTIFICATION) ? "ON" : "OFF");
    } else {
        return WICED_BT_GATT_WRITE_NOT_PERMIT;
    }

    /* Acknowledge GATT_REQ_WRITE; no ack needed for GATT_CMD_WRITE */
    if (opcode == GATT_REQ_WRITE) {
        wiced_bt_gatt_server_send_write_rsp(conn_id, opcode, handle);
    }
    return WICED_BT_GATT_SUCCESS;
}

/* --------------------------------------------------------------------------
 * GATT read handler
 * Returns live health values when the phone reads our characteristics.
 * -------------------------------------------------------------------------- */
static wiced_bt_gatt_status_t app_gatt_read_handler(
    uint16_t conn_id, wiced_bt_gatt_opcode_t opcode,
    wiced_bt_gatt_read_t *p_read)
{
    uint8_t  val8;
    uint32_t val32;
    uint8_t  hr_pkt[2];
    uint8_t *p_val  = NULL;
    uint16_t val_len = 0;

    switch (p_read->handle) {
        case HDLC_WATCHWISE_HEALTH_SP02_VALUE:
            val8    = health_spo2;
            p_val   = &val8;
            val_len = 1u;
            break;
        case HDLC_WATCHWISE_HEALTH_STEPCOUNT_VALUE:
            val32   = health_steps;
            p_val   = (uint8_t *)&val32;
            val_len = (uint16_t)sizeof(val32);
            break;
        case HDLC_HRS_BODY_SENSOR_LOCATION_VALUE:
            val8    = 0x04u;  /* 0x04 = Wrist */
            p_val   = &val8;
            val_len = 1u;
            break;
        case HDLC_HRS_HEART_RATE_MEASUREMENT_VALUE:
            /* flags=0x00 (8-bit HR), then HR value */
            hr_pkt[0] = 0x00u;
            hr_pkt[1] = health_hr;
            p_val   = hr_pkt;
            val_len = 2u;
            break;
        default:
            wiced_bt_gatt_server_send_error_rsp(conn_id, opcode,
                p_read->handle, WICED_BT_GATT_ATTRIBUTE_NOT_FOUND);
            return WICED_BT_GATT_ATTRIBUTE_NOT_FOUND;
    }

    wiced_bt_gatt_server_send_read_handle_rsp(conn_id, opcode,
        val_len, p_val, NULL);
    return WICED_BT_GATT_SUCCESS;
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
                printf("[BLE] Connected  conn_id=%u\n", ble_conn_id);
            } else {
                ble_connected = false;
                spo2_cccd     = 0;
                steps_cccd    = 0;
                hr_cccd       = 0;
                printf("[BLE] Disconnected — restarting advertisements\n");
                wiced_bt_start_advertisements(BTM_BLE_ADVERT_UNDIRECTED_HIGH, 0, NULL);
            }
            break;

        case GATT_ATTRIBUTE_REQUEST_EVT: {
            wiced_bt_gatt_attribute_request_t *req = &p_data->attribute_request;

            if (req->opcode == GATT_REQ_WRITE || req->opcode == GATT_CMD_WRITE) {
                return app_gatt_write_handler(req->conn_id, req->opcode,
                                              &req->data.write_req);
            }
            if (req->opcode == GATT_REQ_READ || req->opcode == GATT_REQ_READ_BLOB) {
                return app_gatt_read_handler(req->conn_id, req->opcode,
                                             &req->data.read_req);
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
void task_ble(void *pvParameters)
{
    /* Start WICED BT stack. BTM_ENABLED_EVT fires asynchronously when ready. */
    wiced_bt_stack_init(app_bt_management_callback, &wiced_bt_cfg_settings);

    TickType_t last_notify = xTaskGetTickCount();

    for (;;) {
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
    xTaskCreate(task_ble, "BLE",
                configMINIMAL_STACK_SIZE * 8, NULL,
                tskIDLE_PRIORITY + 1, NULL);
}

# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

WatchWise is a health monitoring wearable application for Infineon's PSoC6 (Cortex-M4) built with ModusToolbox. It uses FreeRTOS to concurrently read from a MAX30102 PPG sensor (heart rate/SpO2) and BMI270 IMU (motion/steps), then processes sensor data through a real-time health algorithm.

All application code runs on the CM4 core. The primary project directory is `ece453_mtb_base/`.

## Build Commands

All commands run from `ece453_mtb_base/`:

```bash
make getlibs      # First-time setup: download all .mtb dependencies to libs/
make vscode       # Generate .vscode/ configs (tasks, launch, IntelliSense)
make build        # Compile firmware
make program      # Flash to board via KitProg3
make clean        # Remove build artifacts
make erase        # Erase device flash
```

### Build Flags (`DEFINES=`)

Optional compile-time flags select board/power variants. They are independent and composable, and **change nothing when omitted** (default `make program` is byte-identical):

```bash
make program DEFINES=LOW_POWER                       # low-power profile
make program DEFINES=SW_STEP_COUNTER                 # software pedometer (dead-feature-engine board)
make program DEFINES="LOW_POWER SW_STEP_COUNTER"     # both
```

- **`LOW_POWER`** — see "Low-Power Profile" below.
- **`SW_STEP_COUNTER`** — see "BMI270 Step Counter — Hardware Fault & Software Fallback".

There is no test framework. Linting is via clangd (configured in `.clangd`).

## Debug Output

`printf()` is redirected to UART via `cy_retarget_io` on **P5_0 (RX) / P5_1 (TX)** at **115200 baud** over the KitProg3 USB COM port. The system control task prints health metrics every 2 seconds.

For GDB debugging: install the Cortex-Debug VSCode extension, open `ece453_mtb_base/` in VSCode, and press F5 to build, flash, and attach.

## Architecture

### Task Structure

The system uses FreeRTOS (1000 Hz tick, 64 KB heap, 7 priority levels). Task creation is centralized in `task_system_control.c`, which also initializes the I2C bus and UART before launching all other tasks.

```
main.c
  └─ system_control_initialize()
       ├─ I2C init (SCB3, P6_4/P6_5, 400 kHz)
       ├─ i2c_mutex creation (recursive, shared across all sensor tasks)
       ├─ task_max30102 — reads PPG FIFO every 100ms, feeds health_algo
       ├─ task_bmi270   — reads 6-axis IMU every 100ms, step count every 1s
       ├─ task_system_control — prints health_hr, health_spo2, health_steps every 2s
       └─ task_ble — WICED BT stack init + GATT notifications every 2s
```

### I2C Sharing

All sensor I2C transactions are protected by a **recursive mutex** (`i2c_mutex` in `task_system_control.h`). Both sensor tasks take the mutex for the full duration of each read/write sequence. Never access the I2C bus without holding this mutex.

### Sensor Addresses

| Sensor  | I2C Address | Bus Pins        |
|---------|-------------|-----------------|
| MAX30102 | 0x57       | P6_4 SCL, P6_5 SDA |
| BMI270   | 0x68       | P6_4 SCL, P6_5 SDA |

### Health Algorithm (`health_algo.c`)

Processes raw Red and IR FIFO samples from MAX30102:
- Maintains a 100-sample ring buffer (~4 sec at effective 25 Hz)
- DC removal → 5-sample moving average → threshold-based peak detection
- Heart rate computed from RR intervals between peaks
- SpO2 estimated from Red/IR AC-to-DC ratio
- EMA (α=0.25) smooths output; IR DC mean < 20,000 triggers "no finger" state

Outputs are volatile globals: `health_hr`, `health_spo2`, `health_steps`.

### BLE Stack (`task_ble.c`)

The CY8CPROTO-063-BLE board has an **integrated BLESS** peripheral (not an external BT chip). The correct library stack is:
- **`btstack-integration`** (`release-v4.7.0`) — IPC transport adapter bridging WICED BT host to the CM0+ BLESS controller
- **`btstack`** (`release-v3.9.2`) — WICED BT stack, provides `wiced_bt_*` API headers

Do NOT use `bluetooth-freertos` (HCI UART only, for external AIROC chips) or `bless` (different API style, incompatible with BT Configurator v3.60 output).

**Dual-core architecture:**
- CM0+ runs the BLESS link-layer controller (`CM0P_BLESS` firmware, linked by BSP)
- CM4 runs the WICED BT host stack (GAP + GATT) via IPC — the `BLESS-IPC` transport component is activated automatically by the BSP (`DEVICE_COMPONENTS = BLESS-IPC` in `bsp.mk`)
- No explicit `COMPONENTS` entry needed for BLE; the BSP wires it up

**BT Configurator generates WICED BT API code**, not bless `cy_ble_*` code. Generated files:
- `cycfg_gatt_db.h/c` — `gatt_database[]` byte array + `HDLC_*` / `HDLD_*` handle `#define`s
- `cycfg_bt_settings.h/c` — `wiced_bt_cfg_settings` struct instance
- `cycfg_gap.h/c` — `cy_bt_adv_packet_data[]`, `cy_bt_device_address`

**GATT services** (currently defined in BT Configurator):
| Service | UUID | Characteristics |
|---------|------|-----------------|
| WatchWise Health | Custom 128-bit | SP02 (read+notify, handle 0x000A), StepCount (read+notify, handle 0x000D) |

**Key init sequence in `task_ble`:**
1. `wiced_bt_stack_init(app_bt_management_callback, &wiced_bt_cfg_settings)`
2. In `BTM_ENABLED_EVT`: `wiced_bt_gatt_db_init(gatt_database, gatt_database_len, NULL)`
3. `wiced_bt_gatt_register(app_gatt_callback)` — registers connection + GATT request handler
4. `wiced_bt_start_advertisements(BTM_BLE_ADVERT_UNDIRECTED_HIGH, 0, NULL)`
5. Every 2 s: send notifications via `wiced_bt_gatt_server_send_notification()`

**CCCD tracking**: GATT write events on `HDLD_WATCHWISE_HEALTH_SP02_CLIENT_CHAR_CONFIG` (0x000B) and `HDLD_WATCHWISE_HEALTH_STEPCOUNT_CLIENT_CHAR_CONFIG` (0x000E) are stored in `spo2_cccd` / `steps_cccd`. Bit 0 = `GATT_CLIENT_CONFIG_NOTIFICATION`.

**Phone testing:** nRF Connect (iOS/Android) → scan for `WatchWise` → Connect → tap the WatchWise Custom Service → enable notifications (↓↓↓) on SP02 and StepCount.

### BMI270 Initialization

The BMI270 requires loading a large firmware blob (`bmi270_config_file[]` in `bmi270_config.h`, ~8 KB) over I2C at startup before the feature engine (step counter) becomes available. The step counter is **enabled** by setting bits 3–4 of feature page 6, byte 3 (`SC_1.en_detector`/`en_counter`); the live count is **read** from the feature-engine output on **feature page 0, bytes 0–1** (NOT register `0x1E` — that read was a regression that always returned 0).

The 8 KB config-blob load over I2C is flaky (it's the most demanding bus transfer); a single bad load reports a junk `INTERNAL_STATUS` and would kill the IMU task. `bmi270_load_config()` is therefore wrapped in a **retry loop** (soft reset `CMD=0xB6` + retry, up to 5×).

### BMI270 Step Counter — Hardware Fault & Software Fallback

One board (PCB #1) has a **dead BMI270 feature engine**: the accelerometer, gyro, I2C, and config-load all work and the config blob loads byte-identical to a good board, but the on-die feature engine never produces output (step-activity output frozen at `0x00` through sustained motion). That block is internal silicon with no external pin, so it can't be fixed in firmware or by reflow — the chip needs replacing. Diagnosis notes are in `software/debug.txt`.

As a stopgap to keep that board usable without a spare chip, there is a **software pedometer** selected by a build flag:

```bash
make program                            # normal: hardware step counter (good boards)
make program DEFINES=SW_STEP_COUNTER    # workaround: software pedometer (PCB #1)
```

- Implemented entirely in `sw_step_counter.c`/`.h`; **`task_bmi270.c` is untouched**. The only existing-file change is a `#ifdef SW_STEP_COUNTER`-gated spawn in `task_system_control.c`.
- When the flag is set it runs a FreeRTOS task that reads accel directly over I2C at ~50 Hz (its own transactions on the shared `i2c_mutex`), runs a magnitude → DC-removal → peak-detect algorithm (refractory period + 4-step warm-up to reject non-gait motion), and writes the cumulative count into `health_steps`, overriding the always-0 hardware read.
- The whole file compiles to nothing without the flag — normal builds are byte-for-byte unaffected.
- Thresholds (`SWS_TH_HIGH`, refractory, warm-up) are tunable in `sw_step_counter.c` and may need adjusting to how the watch is worn.

### Low-Power Profile (`DEFINES=LOW_POWER`)

Reduces current draw for the "always track steps, measure HR/SpO2 only on demand" usage model. Baseline draw is ~10–15 mA (≈5 h on the 60 mAh cell); these changes target the easy wins (≈+50% runtime) without the deep-sleep plumbing. All changes are `#ifdef LOW_POWER`-gated; the default build is unaffected.

| Area | File | Change when `LOW_POWER` |
|------|------|--------------------------|
| Gyro | `task_bmi270.c` | `PWR_CTRL=0x04` (accel only). Gyro is unused and draws ~0.9 mA; the step counter runs on accel. |
| PPG | `task_max30102.c` | **Connection-gated live** (`max30102_scheduler`): sensor `SHDN` while disconnected; runs **continuously** (live HR+SpO2) while the app is connected (`ble_connected`), stops on disconnect. **Adaptive IR LED** (bumps current when perfusion weak), shorter 118 µs pulse at 100 sps (`SPO2_CONFIG=0x25`, keeps the 25 Hz effective rate). HR estimation (autocorrelation + quality gate) is in `health_algo.c` and applies to both builds. |
| BLE advertising | `task_ble.c` | Advertise at the cfg low-duty interval (`BTM_BLE_ADVERT_UNDIRECTED_LOW`, ~1 s) instead of high-duty 30 ms, via the `WW_ADV_MODE` macro. |
| BLE connection | `task_ble.c` | On connect, request slow link params (`wiced_bt_l2cap_update_ble_conn_params`): ~400–500 ms interval, slave latency 4, 6 s supervision — radio idles between the 2 s notifications. |

**Not yet included (the big multiplier):** FreeRTOS tickless idle + PSoC6 deep sleep (`Cy_SysPm_DeepSleep`). That takes idle from ~mA to ~µA (3–5×) and is what makes the BLE controller sleep between events, but it needs careful integration/testing (deep sleep can break in-flight I2C/UART/BLE) — a separate effort, not a flag flip.

**Future:** true on-demand HR/SpO2 (a writable BLE characteristic triggering a measurement) instead of the firmware duty-cycle; that needs a GATT DB change in BT Configurator.

### Header Abstractions

- `Infineon_Api.h` — wraps all `cyhal_*` HAL includes (I2C, GPIO, UART, ADC, timer)
- `FreeRTOS_Api.h` — wraps all FreeRTOS kernel includes (task, queue, semaphore, timer)
- `system_tasks.h` — aggregates all task headers; controls which tasks are compiled

## Key Files

| File | Role |
|------|------|
| `ece453/FreeRTOS/tasks/src/main.c` | Entry point; BSP init, scheduler start |
| `ece453/FreeRTOS/tasks/src/task_system_control.c` | I2C + UART init, task spawning, metric reporting |
| `ece453/FreeRTOS/tasks/src/task_max30102.c` | MAX30102 FIFO driver |
| `ece453/FreeRTOS/tasks/src/task_bmi270.c` | BMI270 6-axis + step counter driver |
| `ece453/FreeRTOS/tasks/src/task_ble.c` | WICED BT stack init, GAP/GATT callbacks, SpO2+Steps notify loop |
| `ece453/FreeRTOS/tasks/src/health_algo.c` | HR/SpO2 signal processing |
| `ece453/FreeRTOS/tasks/src/sw_step_counter.c` | Software pedometer fallback (built only with `DEFINES=SW_STEP_COUNTER`) |
| `ece453/FreeRTOS/tasks/include/task_ble.h` | BLE task public interface; exposes `ble_connected` |
| `ece453/FreeRTOS/tasks/include/bmi270_config.h` | BMI270 firmware blob (~8 KB array) |
| `ece453/FreeRTOS/FreeRTOSConfig.h` | FreeRTOS kernel configuration |
| `Makefile` | Build config: target board, toolchain, components |
| `deps/btstack-integration.mtb` | WICED BT IPC transport adapter for integrated BLESS (release-v4.7.0) |
| `bsps/.../config/GeneratedSource/cycfg_gatt_db.h/c` | BT Configurator output — GATT database array + handle defines |
| `bsps/.../config/GeneratedSource/cycfg_bt_settings.h/c` | BT Configurator output — `wiced_bt_cfg_settings` |
| `bsps/.../config/GeneratedSource/cycfg_gap.h/c` | BT Configurator output — advertising packet data |

## Build Configuration

- **Target board:** `APP_CY8CPROTO-063-BLE` (PSoC 6 BLE prototyping kit)
- **Toolchain:** GCC ARM, hardware floating point (`hardfp`)
- **Config:** Debug (minimal optimization)
- **Components:** `FREERTOS WICED_BLE` — `WICED_BLE` activates `libbtstack.a` (the pre-compiled WICED BT stack) from the `btstack` library. The `BLESS-IPC` transport is auto-included by the BSP (`DEVICE_COMPONENTS = BLESS-IPC` in `bsp.mk`)
- **ModusToolbox tools path:** `/Applications/ModusToolbox/tools_3.8`
- **Shared libraries:** `../mtb_shared/` (shared across projects, not committed)
- **Downloaded libraries:** `libs/` (gitignored, populated by `make getlibs`)

## BLE Setup: Required One-Time Steps

`btstack-integration` and `btstack` must be fetched, and BT Configurator must generate the GATT database before building.

### Step 1 — Fetch libraries

```bash
cd ece453_mtb_base
make getlibs
```

### Step 2 — Run BT Configurator to define GATT services

Open the BT Configurator GUI:
```
/Applications/ModusToolbox/tools_3.8/bt-configurator/bt-configurator.app
```

1. **New design** → select target **PSoC 6 BLE** (BLESS peripheral)
2. **Add service:** Heart Rate (from SIG profiles list)
   - Enable **Heart Rate Measurement** characteristic → check **Notify**
   - Enable **Body Sensor Location** characteristic → value = `0x04` (Wrist)
3. **Add service:** Custom → name it `WatchWise Health`
   - Add characteristic → name `SpO2`, UUID (128-bit, any unique), properties: **Read + Notify**
   - Add characteristic → name `StepCount`, UUID (128-bit, any unique), properties: **Read + Notify**
4. **GAP / Advertising:**
   - Device name: `WatchWise`
   - Advertising interval: Fast (100 ms), Slow (1000 ms)
   - Include Heart Rate Service UUID in advertising payload
5. **Generate Code** → output directory:
   `ece453_mtb_base/bsps/TARGET_APP_CY8CPROTO-063-BLE/config/GeneratedSource/`

### Step 3 — Verify handles in task_ble.c

Open the generated `cycfg_gatt_db.h` and confirm `HDLC_WATCHWISE_HEALTH_SP02_VALUE` and `HDLC_WATCHWISE_HEALTH_STEPCOUNT_VALUE` are defined. `task_ble.c` uses these directly — no manual updates needed as long as the characteristic names match.

### Step 4 — Build and flash

```bash
make build
make program
```

## WICED BT v3.x GATT Server: Critical Differences from v2.x

In btstack v3.9.2, the stack does **not** handle ATT protocol operations internally. The app GATT callback must handle all of these explicitly:

| Opcode | Name | Required action |
|--------|------|-----------------|
| `0x02` `GATT_REQ_MTU` | MTU Exchange | Call `wiced_bt_gatt_server_send_mtu_rsp(conn_id, remote_mtu, CY_BT_RX_PDU_SIZE)` — stack does NOT auto-respond |
| `0x08` `GATT_REQ_READ_BY_TYPE` | Characteristic discovery | Walk `gatt_database[]` with `wiced_bt_gattdb_local_read_data_by_type(GATT_DISCOVER_CHARACTERISTICS, ...)`, build response buffer, call `wiced_bt_gatt_server_send_read_by_type_rsp()`. For non-0x2803 UUIDs, use `wiced_bt_gatt_find_handle_by_type()` then serve via the read handler. |
| `0x0A` `GATT_REQ_READ` | Attribute read | Look up handle in `app_gatt_db_ext_attr_tbl[]` (from BT Configurator). For dynamic handles (SpO2, HR, StepCount) update the backing array first. Call `wiced_bt_gatt_server_send_read_handle_rsp()`. |
| `0x12` `GATT_REQ_WRITE` | Attribute write | Update `app_gatt_db_ext_attr_tbl[]` backing array. Mirror CCCD writes into shadow variables. Call `wiced_bt_gatt_server_send_write_rsp()`. |
| `0x1B` `GATT_HANDLE_VALUE_NOTIF` | Notification-sent confirmation | Return `WICED_BT_GATT_SUCCESS` — no action needed. |
| `0x1E` `GATT_HANDLE_VALUE_CONF` | Indication confirmation from client | Return `WICED_BT_GATT_SUCCESS` — no action needed (we use notifications, not indications). |

**`GATT_GET_RESPONSE_BUFFER_EVT`** — fires before any ATT response packet is sent. Must allocate a buffer and set `p_data->buffer_request.buffer.p_app_rsp_buffer = wiced_bt_get_buffer(len_requested)` with `p_app_ctxt = wiced_bt_free_buffer`. Without this, all read responses and notifications fail silently.

**WICED BT heap** — `WICED_DEFAULT_HEAP` is `NULL` by default; `wiced_bt_get_buffer()` always fails unless you first call `wiced_bt_create_heap("name", buf, size, NULL, WICED_TRUE)`. Do this before `cybt_platform_config_init()`. 4 KB is sufficient for a GATT server with 512-byte MTU.

**Service discovery** (`GATT_REQ_READ_BY_GRP_TYPE = 0x10`) **is** handled internally by the stack from the registered `gatt_database[]` — do NOT handle it in the app callback.

**`featureMask` compatibility** — CM0P_BLESS 5.0.8 (cat1cm0p v1.10.0) only accepts `featureMask = 0x0001` (DLE only). The original `0x000B` (DLE+Privacy+PHY) causes `Cy_BLE_StackSetFeatureConfig()` to return `CY_BLE_ERROR_INVALID_PARAMETER`, which silently prevents `BTM_ENABLED_EVT` from ever firing. This is set in `mtb_shared/btstack-integration/release-v4.7.0/COMPONENT_BLESS-IPC/platform/ipc/PSoC6IPC.h`.

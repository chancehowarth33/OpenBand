# WatchWise Firmware

WatchWise is a health-monitoring wearable built on Infineon's **PSoC 6 BLE** (`CY8CPROTO-063-BLE`, Cortex-M4 + Cortex-M0+) using **ModusToolbox** and **FreeRTOS**. It reads a **MAX30102** PPG sensor (heart rate / SpO2) and a **BMI270** IMU (motion / hardware step counter), runs a real-time health algorithm, and streams the results over **BLE**.

All application code runs on the **CM4** core; the **CM0+** core runs the BLESS Bluetooth link-layer controller. The firmware project lives in [`firmware/`](firmware).

---

## Table of contents

1. [Build & flash](#build--flash)
2. [Build flags](#build-flags)
3. [Architecture](#architecture)
4. [Sensors](#sensors)
5. [Health algorithm](#health-algorithm)
6. [Bluetooth (BLE)](#bluetooth-ble)
7. [Step counter](#step-counter)
8. [Low-power profile](#low-power-profile)  ← the power-saving changes
9. [Programming notes](#programming-notes)
10. [File map](#file-map)

---

## Build & flash

All commands run from `firmware/`:

```bash
make getlibs      # First-time: download .mtb dependencies into libs/
make build        # Compile firmware
make program      # Flash to board via KitProg3
make clean        # Remove build artifacts
make erase        # Erase device flash
```

> **After the first `make getlibs`, apply the vendor patch in [`../patches/`](../patches).**
> Without it the firmware still compiles, but the BLE controller never starts:
> `Cy_BLE_StackSetFeatureConfig` rejects the stock feature mask. See
> [`../patches/README.md`](../patches/README.md) for the one-line `git apply` and why it is needed.

- **Toolchain:** GCC ARM, hardware floating point (`hardfp`)
- **Target:** `APP_CY8CPROTO-063-BLE`
- **Debug output:** `printf` → UART via `cy_retarget_io` on **P5_0 (RX) / P5_1 (TX)** at **115200 baud** over the KitProg3 USB COM port.

---

## Build flags

Optional compile-time flags select board/power variants via `DEFINES=`. They are **independent and composable**, and **change nothing when omitted** (plain `make program` is byte-identical to the default build):

```bash
make program DEFINES=LOW_POWER                      # low-power profile
make program DEFINES=SW_STEP_COUNTER                # software pedometer fallback
make program DEFINES="LOW_POWER SW_STEP_COUNTER"    # both
```

| Flag | Purpose |
|------|---------|
| `LOW_POWER` | Battery-saving profile — see [Low-power profile](#low-power-profile). |
| `SW_STEP_COUNTER` | Replaces the hardware step counter with a software pedometer, for a board whose BMI270 feature engine is dead — see [Step counter](#step-counter). |

---

## Architecture

FreeRTOS (1000 Hz tick, 64 KB heap, 7 priority levels). Task creation is centralized in `task_system_control.c`, which initializes the I2C bus + UART before launching the other tasks.

```
main.c
  └─ system_control_initialize()
       ├─ I2C init (SCB3, P6_4 SCL / P6_5 SDA, 400 kHz)
       ├─ i2c_mutex (recursive, shared by all sensor tasks)
       ├─ task_max30102        — PPG FIFO → health_algo
       ├─ task_bmi270          — 6-axis IMU + hardware step counter
       ├─ task_sw_steps        — software pedometer (only if SW_STEP_COUNTER)
       ├─ task_system_control  — prints health metrics every 2 s
       └─ task_ble             — WICED BT stack + GATT notifications every 2 s
```

**I2C sharing:** every sensor transaction is protected by the recursive mutex `i2c_mutex` (in `task_system_control.h`). Never touch the bus without holding it.

| Sensor | I2C address | Bus |
|--------|-------------|-----|
| MAX30102 | `0x57` | P6_4 SCL / P6_5 SDA |
| BMI270 | `0x68` | P6_4 SCL / P6_5 SDA |

Outputs are `volatile` globals consumed by the system-control and BLE tasks: `health_hr`, `health_spo2`, `health_steps`, `health_battery`.

---

## Sensors

### MAX30102 (heart rate / SpO2) — `task_max30102.c`
- Runs in SpO2 mode (Red + IR), reads the FIFO every 100 ms, feeds `health_algo_update_ppg(red, ir)`.
- Default LED drive ~7 mA each, 100 sps, 411 µs pulse (18-bit).

### BMI270 (motion / steps) — `task_bmi270.c`
- Loads an ~8 KB firmware config blob (`bmi270_config.h`) over I2C at boot before the feature engine is available.
- Reads 6-axis data every 100 ms; reads the hardware step count every 1 s.
- See [Step counter](#step-counter) for enable/read details and the config-load retry loop.

---

## Health algorithm — `health_algo.c`

Processes raw Red/IR FIFO samples over a 100-sample ring buffer (~4 s at effective 25 Hz):
- DC removal → 5-sample moving average.
- **Heart rate (frequency-domain primary):** autocorrelation finds the heartbeat period (the strongest self-similarity lag in the 40–200 bpm band) — robust to noise. A threshold **peak detector** runs as a cross-check/fallback, and the estimate is **quality-gated** by the perfusion index so a weak/motion-corrupted window holds the last value instead of reporting noise.
- **SpO2** from the Red/IR AC-to-DC ratio (ratio-of-ratios), gated by `health_algo_set_spo2_enabled()` (the scheduler disables it during IR-only HR windows).
- **Perfusion index** (`health_algo_perfusion()`, AC/DC of IR in %) is exposed as a signal-quality metric, used for adaptive LED current and quality-gating.
- EMA (α = 0.25) smoothing; IR DC mean < 20,000 → "no finger".
- Outputs `health_hr`, `health_spo2`.

These estimator improvements apply to **both** builds; the [tiered scheduling / motion-gating / adaptive-LED](#low-power-profile) parts are `LOW_POWER`-only.

---

## Bluetooth (BLE)

The `CY8CPROTO-063-BLE` has an **integrated BLESS** peripheral (not an external BT chip). The stack is **`btstack-integration`** (IPC transport, `release-v4.7.0`) + **`btstack`** (WICED BT host, `release-v3.9.2`). The GATT database is generated by **BT Configurator**.

**GATT services:**

| Service | Characteristics |
|---------|-----------------|
| WatchWise Health (custom 128-bit) | SpO2 (read+notify), StepCount (read+notify, uint32 LE) |
| Heart Rate (`0x180D`) | HR Measurement (`0x2A37`) |
| Battery (`0x180F`) | Battery Level (`0x2A19`) |

Custom UUIDs:
- Service `5E021176-5E66-483F-AE53-D3291D2CC787`
- SpO2 `F1F72B4B-1D76-43A3-ADB2-0E04905ACC8F`
- StepCount `C36072C1-2706-41F1-8D28-E0619FE31FFA`

Init sequence: `wiced_bt_stack_init` → on `BTM_ENABLED_EVT`, `wiced_bt_gatt_db_init` + `wiced_bt_gatt_register` + start advertising → notify subscribed characteristics every 2 s. The app GATT callback handles all ATT operations explicitly (MTU, read-by-type, read, write, buffer allocation) because btstack v3.x does not. Advertising is **re-armed from the task loop** (via `wiced_bt_ble_get_current_advert_mode()`) so the device stays discoverable past the configured advertising-duration timeout, without racing the connection handshake.

**Phone testing:** nRF Connect → scan for `WatchWise` → connect → enable notifications on SpO2 / StepCount.

---

## Step counter

The BMI270's internal **feature engine** does the step counting:
- **Enable:** set bits 3–4 of feature **page 6, byte 3** (`SC_1.en_detector` / `en_counter` = `0x18`).
- **Read:** the live count is published on the feature-engine output at **feature page 0, bytes 0–1** — read it with `bmi270_read_feat_page(0, …)`. (Reading register `0x1E` is wrong for this config blob and always returns 0 — that was a regression that's since been fixed.)
- **Config-load retry:** the 8 KB blob load over I2C is flaky (most demanding bus transfer). A bad load reports a junk `INTERNAL_STATUS` and would kill the IMU task, so `bmi270_load_config()` retries with a soft reset (`CMD=0xB6`) up to 5×.

### Software pedometer fallback (`DEFINES=SW_STEP_COUNTER`)

If a board's BMI270 **feature engine is dead** (accel/gyro/I2C/config-load all work, but the engine never produces output — e.g. a cold solder joint starving the digital core), build with `SW_STEP_COUNTER`. A self-contained task (`sw_step_counter.c`) reads accel directly over I2C at ~50 Hz and runs a magnitude → DC-removal → peak-detect pedometer (refractory period + warm-up to reject non-gait motion), writing `health_steps` and overriding the hardware read. `task_bmi270.c` is untouched; the only existing-file hook is a flag-gated spawn in `task_system_control.c`. The file compiles to nothing without the flag. Thresholds (`SWS_TH_HIGH`, etc.) are tunable. *(Note: the original "dead engine" board turned out to be a cold solder joint — re-reflowing the BMI270 restored hardware step counting; this build remains as a fallback.)*

---

## Low-power profile

`DEFINES=LOW_POWER` reduces current draw for the **"always track steps; measure HR/SpO2 only on demand"** usage model. Baseline draw is **~10–15 mA** (≈5 h on the 60 mAh cell). These changes are the easy wins (target ≈ +50% runtime). **All changes are `#ifdef LOW_POWER`-gated — the default build is unaffected.**

| Area | File | Change when `LOW_POWER` |
|------|------|--------------------------|
| **Gyro** | `task_bmi270.c` | `PWR_CTRL=0x04` (accel only). Gyro is unused and draws ~0.9 mA; the step counter runs on accel. |
| **PPG — connection-gated live** | `task_max30102.c` | HR/SpO2 only matter when the user is looking, so the sensor is fully `SHDN` while **disconnected**, and runs **continuously (live HR+SpO2)** while the **app is connected** (`ble_connected`) — so the values actually change each ~1 s and the BLE notify carries live data instead of repeating a stale value. On disconnect it shuts down and clears the readings. Steps keep counting regardless. No GATT change needed — the connection is the trigger. |
| **PPG — motion gating** | `task_max30102.c` | A window only starts when the wrist is **still** (accelerometer magnitude variance below `SCH_MOTION_VAR`); otherwise it defers. Cleaner signal + no wasted LED energy during motion. |
| **PPG — adaptive LED** | `task_max30102.c` | IR drive starts low (~4.8 mA) and bumps to ~8.4 mA only if the **perfusion index** (`health_algo_perfusion()`) shows a weak pulse — mirrors Apple "increase brightness when signal is low". |
| **PPG — shorter pulse** | `task_max30102.c` | `SPO2_CONFIG=0x25`: 100 sps, 118 µs (16-bit) vs 411 µs — less LED energy. (Stays 100 sps so the post-FIFO-averaging rate remains 25 Hz, which `health_algo` assumes.) |
| **BLE advertising** | `task_ble.c` | Advertise at the low-duty interval (`BTM_BLE_ADVERT_UNDIRECTED_LOW`, ~1 s) instead of high-duty 30 ms, via the `WW_ADV_MODE` macro. |
| **BLE connection** | `task_ble.c` | On connect, request slow link params (`wiced_bt_l2cap_update_ble_conn_params`): ~400–500 ms interval, slave latency 4, 6 s supervision — the radio idles between the 2 s notifications. |

### Estimated battery life

Computed from the **measured ~12 mA baseline** (0.01–0.02 A on a meter in series with the 60 mAh cell). The component split below is datasheet-grade — the MCU figure is the biggest unknown — but the total is anchored to the measurement.

**Baseline budget (~12 mA):**

| Consumer | Est. draw | Note |
|----------|-----------|------|
| MCU (CM4 + CM0 + BLE, never sleeps) | ~7.0 mA | dominant; ~78% of total |
| MAX30102 (2 LED, 7 mA, 100 sps) | ~1.5 mA | ~0.6 mA IC + ~0.9 mA LED |
| BMI270 (accel + gyro) | ~1.0 mA | gyro ~0.9 of that |
| BLE radio (advert 30 ms + bursts) | ~2.0 mA | |
| LDO quiescent / misc | ~0.5 mA | |

**Cumulative effect of each change:**

| Change (cumulative) | Draw | Life (60 mAh) | vs baseline |
|---------------------|------|---------------|-------------|
| Baseline (everything on) | 12.0 mA | **5.0 h** | — |
| + Gyro off | 11.2 mA | 5.4 h | +8% |
| + PPG HR-only / low current / 50 sps / finger-gate | 10.6 mA | 5.7 h | +13% |
| **+ BLE slow advert + latency** ← full `LOW_POWER` | **9.0 mA** | **6.7 h** | **+34%** |
| + Tickless deep sleep *(future)* | 3.0 mA | 20.3 h | +306% (4.1×) |
| + Off-wrist deep sleep *(future)* | ~0.1 mA | days–weeks | idle, not worn |

**How each moves the needle:**
- **Gyro off** (−0.85 mA): small but free.
- **PPG low-power** (−0.55 mA): the LED reductions are large proportionally (LED average ~0.58 → ~0.03 mA), but the MAX30102's ~0.9 mA IC operating current stays on because finger-gating keeps the chip awake to detect contact — so the realistic PPG win is modest unless the idle path also periodically `SHDN`s the sensor.
- **BLE advertising slowdown + latency** (−1.65 mA): the biggest single easy win — the radio was ~2 mA advertising at 30 ms; at 1 s it's a fraction of that.

**Takeaway:** the easy `LOW_POWER` changes get ~12 → ~9 mA (**5 h → ~6.7 h, +34%**) — "dies mid-afternoon" → "lasts the workday." They cap there because the **~7 mA always-awake MCU is 78% of the baseline** and nothing here touches it. **Tickless deep sleep is the real multiplier (~4× → ~20 h)** and is also what lets the BLE controller sleep between events; **off-wrist sleep** pushes standby to days/weeks. Numbers are estimates ±, but the meter will confirm the deep-sleep drop (0.01 → ~0.00 A).

### Tuning

PPG thresholds/currents in `task_max30102.c` are starting values and depend heavily on the optics / skin coupling:
- If it never detects a finger (never ramps to measure), lower `LP_FINGER_ON`.
- If HR is noisy, raise `LP_IR_MEASURE_PA`.

### Not yet included
- **Tickless idle + PSoC 6 deep sleep** (`Cy_SysPm_DeepSleep`) — the real multiplier (idle ~mA → ~µA, and what lets the BLE controller sleep between events). Needs careful integration/testing (deep sleep can break in-flight I2C/UART/BLE), so it's a separate effort, not a flag flip.
- **True on-demand HR/SpO2** — a writable BLE characteristic that triggers a measurement (and re-enables the Red LED for a real SpO2 reading), instead of the firmware-side gating. Requires a GATT DB change in BT Configurator.

---

## Programming notes

- **Reset pulse:** the Makefile sets a wider XRES reset pulse for `program`/`erase` (`adapter srst pulse_width 200; delay 500`). JPROG1 has an RC filter on `XRES_N`; the stock 25 µs pulse is too short to reliably reset the chip past the filter, which makes `acquire` fail. The wider pulse fixes it.
- **Hand-held JPROG contact:** if `make program` fails with `acquire_psoc request failed` / `cannot read IDR`, press the JPROG1 pins in firmly (esp. XRES_N, GND, SWDIO/SWDCLK) and retry; the BLE firmware sleeps the chip, so the reset has to catch it.
- **`BMI270_debug.txt`** holds the full diagnosis trail for the BMI270 feature-engine / step-counter investigation.

---

## File map

| File | Role |
|------|------|
| `app/FreeRTOS/tasks/src/main.c` | Entry point; BSP init, scheduler start |
| `…/task_system_control.c` | I2C + UART init, task spawning, metric reporting |
| `…/task_max30102.c` | MAX30102 PPG driver (+ LOW_POWER HR-only/finger-gating) |
| `…/task_bmi270.c` | BMI270 driver + hardware step counter (+ LOW_POWER gyro-off) |
| `…/sw_step_counter.c` | Software pedometer fallback (SW_STEP_COUNTER only) |
| `…/task_ble.c` | WICED BT stack, GATT callbacks, notify loop (+ LOW_POWER BLE tuning) |
| `…/health_algo.c` | HR/SpO2 signal processing |
| `…/include/bmi270_config.h` | BMI270 firmware blob (~8 KB) |
| `Makefile` | Build config, components, programming reset-pulse override |
| `bsps/.../config/GeneratedSource/cycfg_*` | BT Configurator output (GATT DB, settings, advertising) |
| `BMI270_debug.txt` | BMI270 step-counter diagnosis log |

See [`CLAUDE.md`](CLAUDE.md) for additional guidance.

# WatchWise

A wearable health-tracking device designed to attach to the back of a watch. WatchWise tracks basic health statistics including step count, heart rate, and blood oxygen (SpO2) levels.

## Project Overview

WatchWise is a compact PCB module that mounts on the back of a standard watch. It uses a PSoC6 microcontroller (via the CYBLE-416045-02 BLE module) running FreeRTOS to collect and process data from an optical heart rate/SpO2 sensor and an IMU for motion tracking. The board includes onboard LiPo charging and battery level monitoring.

## Repository Structure

```text
PSoC6/
├── altium/                          # PCB design files
│   ├── altium_project/              # Altium project and schematic sheets
│   └── watchwise_main/              # Main PCB layout and output files
│
├── CYBLE-416045-02_device/          # CYBLE module datasheets and reference docs
│   ├── infineon-cyble-416045-02-datasheet-en.pdf
│   ├── infineon-an91445-antenna-design-and-rf-layout-guidelines-applicationnotes-en.pdf
│   └── ...
│
├── software/                        # Firmware source code
│   └── ece453_mtb_base/
│       ├── main.c                   # Entry point — initializes BSP, starts FreeRTOS scheduler
│       ├── Makefile                 # ModusToolbox build system
│       ├── bsps/TARGET_APP_CY8CPROTO-063-BLE/  # Board support package for PSoC6 BLE proto board
│       ├── deps/                    # Library dependencies (FreeRTOS, retarget-io)
│       └── ece453/
│           └── FreeRTOS/
│               ├── FreeRTOSConfig.h # FreeRTOS configuration
│               └── tasks/
│                   ├── include/     # Task headers and API definitions
│                   └── src/
│                       └── task_system_control.c  # Main system control task (motor GPIO, UART)
│
├── WatchWise-BOM.csv                # Bill of materials with DigiKey part numbers
└── digikey_order.csv                # DigiKey order confirmation
```

## Bill of Materials

Total BOM cost: **~$131.52** (ordered qty ×3 for 3 boards).

### ICs & Modules

| Designator | Part | Description | DigiKey P/N |
|------------|------|-------------|-------------|
| U3_CYBLE1 | CYBLE-416045-02 | PSoC6 BLE module with integrated antenna (SMD) | 448-CYBLE-416045-02CT-ND |
| U2_MAX1 | MAX30102EFD+T | Optical heart rate and SpO2 sensor (I2C) | MAX30102EFD+TCT-ND |
| U1_IMU1 | BMI270 | 6-axis IMU — accelerometer and gyroscope (I2C/SPI) | 828-1091-1-ND |
| U4_CHAR1 | MCP73831T-2ACI/OT | Single-cell LiPo battery charger IC (SOT-23-5) | MCP73831T-2ACI/OTCT-ND |
| U5_1P8 | TLV70018DCKR | 1.8V LDO regulator, 200mA (SC-70-5) | 296-32409-1-ND |

### Indicators

| Designator | Part | Description | DigiKey P/N |
|------------|------|-------------|-------------|
| P_BATT1 | LTST-C281KGKT | Green LED — battery status indicator (0402) | 160-LTST-C281KGKTCT-ND |
| P_CHAR1 | VLMG1500-GS08 | Yellow-green LED — charge status indicator (0402) | VLMG1500-GS08CT-ND |

### Crystal

| Designator | Part | Description | DigiKey P/N |
|------------|------|-------------|-------------|
| RTC1 | MU00525-32.768K | 32.768 kHz crystal, 6pF load, SMD 3.2×1.5mm | 644-MU00525-32.768KCT-ND |

### Resistors

| Designator | Value | Description | DigiKey P/N |
|------------|-------|-------------|-------------|
| R1, R2, R3 | 4.7kΩ | I2C pull-ups (0402) | 311-4.7KJRCT-ND |
| R4, R_PWR1 | 10kΩ | General pull-up/down resistors (0402) | 311-10KJRCT-ND |
| RLED1 | 470Ω | LED current-limiting resistor (0402) | 311-470LRCT-ND |
| R_PROG1 | 47kΩ | MCP73831 charge-current programming resistor — sets ~21mA charge rate (0402) | 311-47.0KLRCT-ND |
| RD1 | 100kΩ | Battery voltage divider — lower leg (0402) | 311-100KLRCT-ND |
| RD2 | 220kΩ | Battery voltage divider — upper leg (0402) | 311-220KLRCT-ND |

> RD1 + RD2 form a resistor divider for ADC-based battery level sensing. R_PROG1 programs the MCP73831 charge current: I_CHG = 1000V / R_PROG(kΩ) ≈ 21mA.

### Capacitors

| Designator | Value | Package | Notes | DigiKey P/N |
|------------|-------|---------|-------|-------------|
| C1, C2, C4, C6, C10 | 0.1µF | 0402 | Bypass caps | 1276-1002-1-ND |
| C11, C12 | 1µF | 0402 | Bypass/decoupling | 399-C0402C105K9PACTUCT-ND |
| C3 | 4.7µF | 0402 | Decoupling — low-voltage rail | 1276-1482-1-ND |
| C13, C14 | 4.7µF | 0603 | Decoupling — BAT_OUT rail (3.7–4.2V); 0603 required to avoid derating | 1276-1044-1-ND |
| C9 | 10µF | 0603 | Bulk decoupling | 1292-0603X106M100CT-ND |
| C5 | 10µF | 0805 | Bulk decoupling — BAT_OUT rail (3.7–4.2V); 0805 required to avoid derating | 1276-1096-1-ND |
| C7, C8 | 6pF | 0402 | RTC crystal load capacitors | 490-3104-1-ND |

> C5, C13, and C14 are sized at 0805/0603 rather than 0402 because ceramic capacitor capacitance drops significantly under DC bias. Since BAT_OUT runs at 3.7–4.2V, the larger packages are needed to maintain rated capacitance.

## Firmware

The firmware is built with **Infineon ModusToolbox** targeting the **CY8CPROTO-063-BLE** development board BSP (compatible with the PSoC6 BLE chip inside the CYBLE-416045-02 module). It uses **FreeRTOS** for task scheduling.

- `main.c` — initializes board peripherals and starts the FreeRTOS scheduler
- `task_system_control` — the primary FreeRTOS task; currently configures motor control GPIO (P10.0–P10.3) and runs the main control loop
- UART retarget-io is initialized on P5_0/P5_1 at 115200 baud for debug output

### Build

Open the project in ModusToolbox or run:

```bash
make build
```

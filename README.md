# WatchWise

A wearable health-tracking device designed to attach to the back of a watch. WatchWise tracks basic health statistics including step count, heart rate, and blood oxygen (SpO2) levels.

## Project Overview

WatchWise is a compact PCB module that mounts on the back of a standard watch. It uses a PSoC6 microcontroller running FreeRTOS to collect and process data from an optical heart rate/SpO2 sensor and an IMU for motion tracking.

## Repository Structure

```text
watch_wise/
├── altium/                          # PCB design files
│   └── ece453-student-cyble-416045-02-full-custom/
│       ├── *.SchDoc                 # Schematic sheets (MCU, components, programming/reset)
│       ├── ECE453.PcbDoc            # PCB layout
│       ├── ECE453-Custom-Proto.PrjPcb  # Altium project file
│       ├── ECE453.OutJob            # Output job (gerbers, BOM, etc.)
│       ├── 3D/                      # 3D models for PCB components
│       ├── Templates/               # Schematic sheet templates
│       └── uw-altium-parts/         # UW-Madison custom Altium parts library
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
└── WatchWise-BOM - Sheet1.pdf       # Bill of materials
```

## Bill of Materials

### Purchased on Breakout Board

| Part | Description |
|------|-------------|
| PSoC6 MCU (CY8CPROTO-063-BLE) | Microcontroller — Infineon PSoC6 with BLE, runs FreeRTOS firmware |
| MAX30102 | Heart rate and blood oxygen (SpO2) optical sensor |
| BMI270 | 6-axis accelerometer and gyroscope (SparkFun breakout) |

### Discrete Components (Not Pre-Purchased)

| Part | Description |
|------|-------------|
| XC620P182MR-G | LDO voltage regulator — 3.3V to 1.8V (for sensors requiring 1.8V) |
| TYPE-C-31-M-12 | USB-C connector for charging |
| AMS1117-3.3 / NCP167AMX330TBG | LDO voltage regulator — 5V to 3.3V |
| MCP73831 | Single-cell LiPo battery charger IC |
| DW01 | Battery protection IC (overcurrent, overvoltage, short circuit) |
| PTS810SJG250SMTRLFS | Tactile push buttons (reset / programming) |

## Firmware

The firmware is built with **Infineon ModusToolbox** targeting the **CY8CPROTO-063-BLE** development board. It uses **FreeRTOS** for task scheduling.

- `main.c` — initializes board peripherals and starts the FreeRTOS scheduler
- `task_system_control` — the primary FreeRTOS task; currently configures motor control GPIO (P10.0–P10.3) and runs the main control loop
- UART retarget-io is initialized on P5_0/P5_1 at 115200 baud for debug output

### Build

Open the project in ModusToolbox or run:

```bash
make build
```

3.3V+ → 3.3V: AP2112K-3.3
3.3V+ → 1.8V: TLV70018

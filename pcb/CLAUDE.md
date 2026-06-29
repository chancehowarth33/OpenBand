# WatchWise PCB — Schematic & Layout Reference

All design files live in two directories:
- `altium_project/` — original Altium source (binary, not easily readable)
- `watchwise_main_kicad/` — KiCAD port (readable, use this for reference)

KiCAD schematic sheets:
- `watchwise_main.kicad_sch` — top-level hierarchy
- `02-Components.kicad_sch` — sensors (MAX30102, BMI270)
- `03-MCU.kicad_sch` — PSoC6 BLE module
- `04-Programming_Reset.kicad_sch` — SWD programming header + reset
- `05-power.kicad_sch` — battery charger + LDOs
- `power.kicad_sch` — secondary power sheet (3.3V + 1.8V LDOs)

---

## Power Architecture

```
USB 5V (J1CHAR1)
    │
    ▼
U4_CHAR1 (MCP73831-2-OT) ──── R_PROG1 (47kΩ) → 21.3mA charge current
    │                          RLED1 (470Ω) + P_CHAR1 LED → charging indicator
    ▼
BAT_OUT (3.7V–4.2V) ◄──── J2BATT1 (battery, 2-pin 1.27mm)
    │
    ├──► U1 (AP2112K-3.3) ──► 3.3_VCC (3.3V, 600mA max)
    │
    └──► U5_1P8 / U2 (TLV70018) ──► +1V8 (1.8V, 200mA max)
                                          │
                                          ├──► CYBLE-416045-02 (core supply)
                                          ├──► MAX30102 (VDD)
                                          ├──► BMI270 (VDD)
                                          └──► JPROG1 Pin 5 (VTARG)
```

### Power Rails Summary

| Rail | Voltage | Source | Max Current | Consumers |
|------|---------|--------|-------------|-----------|
| BAT_OUT | 3.7–4.2V | LiPo battery | Battery limited | LDO inputs, charger output |
| 3.3_VCC | 3.3V | AP2112K-3.3 (U1) | 600mA | Available for peripherals |
| +1V8 | 1.8V | TLV70018 (U5_1P8/U2) | 200mA | PSoC6, all sensors, VTARG |

### Charging Circuit (U4_CHAR1 — MCP73831-2-OT)

- **Input:** 4.5V–6V on J1CHAR1 (2-pin 1.27mm header)
- **Output:** 4.2V CC/CV to battery via J2BATT1
- **Charge current:** 21.3mA (R_PROG1 = 47kΩ). Formula: `I = 1000 / R_PROG_ohms`
- **For 60mAh battery at 0.5C:** replace R_PROG1 with 33kΩ → 30.3mA
- **Status LED:** P_CHAR1 ON = charging, OFF = complete or no input
- **STAT pin (Pin 5):** ~0V when charging, Hi-Z when complete

---

## Component Inventory

### Sheet 02 — Sensors

| Ref | Part | Value / Description | I2C Addr | Supply |
|-----|------|--------------------|---------:|--------|
| U2_MAX1 | MAX30102EFD+T | PPG / SpO2 / Heart Rate | 0x57 | +1V8 |
| U1_IMU1 | BMI270 | 6-axis IMU (accel + gyro + step) | 0x68 | +1V8 |
| R1, R2 | Resistor | 4.7kΩ I2C pull-ups (MAX30102) | — | +1V8 |
| R3 | Resistor | 4.7kΩ I2C pull-up (BMI270) | — | +1V8 |
| C1, C2, C4, C6 | Cap | 0.1µF decoupling | — | — |
| C3 | Cap | 4.7µF bulk bypass | — | — |
| C5 | Cap | 10µF bulk bypass | — | — |

**Shared I2C bus:** Both sensors on `SDA` / `SCL` → PSoC6 SCB3 (P6_4 SCL, P6_5 SDA) at 400 kHz.
**Interrupts:** `INT_IMU` (BMI270 INT1), `INT_MAX_N` (MAX30102, active-low) routed to CYBLE GPIO.

### Sheet 03 — MCU

| Ref | Part | Description |
|-----|------|-------------|
| U3_CYBLE1 | CYBLE-416045-02 | PSoC6 BLE module (CY8C6347BZI-BLD53 + antenna + crystal) |
| RTC1 | 32.768 kHz crystal | WCO for PSoC6 RTC and low-power timing |
| C7, C8 | 6pF | WCO crystal load capacitors |
| C9 | 10µF | CYBLE module bulk decoupling |
| C10 | 0.1µF | CYBLE module bypass |
| RD1 | 100kΩ | Pull-down resistor |
| RD2 | 220kΩ | Reset/enable control |

**CYBLE-416045-02 key facts:**
- Contains: CY8C6347BZI-BLD53 SoC + integrated chip antenna + 32 MHz ECO + RF matching — pre-certified FCC/IC/CE
- Supply: 1.71V–3.6V (powered from +1V8)
- BLE: integrated BLESS peripheral on CM0+ core; CM4 runs application

### Sheet 04 — Programming / Reset

| Ref | Part | Description |
|-----|------|-------------|
| JPROG1 | PinHeader_1x05_P1.27mm | SWD programming header (DNP by default, solder to use) |
| R4 | Resistor | XRES_N pull-up/filter |

**JPROG1 Pinout:**

| Pin | Net | Signal |
|-----|-----|--------|
| 1 | SWDIO | SWD data |
| 2 | SWDCLK | SWD clock |
| 3 | XRES_N | Reset (active-low) |
| 4 | GND | Ground |
| 5 | +1V8 | VTARG (KitProg3 voltage sense) |

**KitProg3 connection:** VTARG→Pin5, SWDIO→Pin1, GND→Pin3, SWDCLK→Pin2, XRES_N→Pin3.
Battery must be connected to power the +1V8 rail (VTARG) before programming.

### Sheet 05 — Power Management

| Ref | Part | Value | Description |
|-----|------|-------|-------------|
| U4_CHAR1 | MCP73831-2-OT | — | Single-cell LiPo charger, SOT-23-5 |
| U5_1P8 | TLV70018_SOT353 | — | 1.8V LDO (input from BAT_OUT) |
| J1CHAR1 | Conn_01x02, 1.27mm | "J3 Charging" | USB 5V input header (DNP by default) |
| J2BATT1 | Conn_01x02, 1.27mm | "J2 Battery" | LiPo battery header |
| R_PROG1 | Resistor 0402 | 47kΩ | Sets MCP73831 charge current |
| RLED1 | Resistor 0402 | 470Ω | Charge status LED current limit |
| P_CHAR1 | LED 0402 | — | Charge status indicator |
| P_BATT1 | LED 0402 | — | Battery status indicator |
| C11 | Cap 0402 | 1µF | VBAT bypass |
| C14 | Cap 0603 | 4.7µF | Charger input bypass |

### Sheet: power.kicad_sch — LDO Rail Generation

| Ref | Part | Input | Output | Max I |
|-----|------|-------|--------|-------|
| U1 | AP2112K-3.3 | BAT_OUT | 3.3_VCC | 600mA |
| U2 | TLV70018_SOT353 | 3.3_VCC or BAT_OUT | +1V8 | 200mA |
| C1, C2 | 0.1µF / 1µF | — | Input filter U1 | — |
| C3, C4 | 1µF | — | Output filter | — |

---

## Cross-Sheet Net Reference

| Net | Type | Source Sheet | Destination Sheets | Notes |
|-----|------|--------------|--------------------|-------|
| BAT_OUT | Power | 05-power | All | Raw battery voltage 3.7–4.2V |
| +1V8 | Power | power / 05-power | 02, 03, 04 | Core logic supply |
| 3.3_VCC | Power | power | Available | Not currently consumed by any major load |
| SDA | Signal | 02-Components | 03-MCU | I2C data, 4.7kΩ pull-up to +1V8 |
| SCL | Signal | 02-Components | 03-MCU | I2C clock, 4.7kΩ pull-up to +1V8 |
| INT_IMU | Signal | 02-Components | 03-MCU | BMI270 INT1, active-high |
| INT_MAX_N | Signal | 02-Components | 03-MCU | MAX30102 interrupt, active-low |
| SWDIO | Signal | 04-Programming | 03-MCU | SWD data |
| SWDCLK | Signal | 04-Programming | 03-MCU | SWD clock |
| XRES_N | Signal | 04-Programming | 03-MCU | PSoC6 reset, active-low |

---

## Key Measurements (Board Powered, Battery Connected)

| Test Point | Expected | Condition |
|------------|----------|-----------|
| J2BATT1 Pin1 (BAT_OUT) | 3.7–4.2V | Battery connected |
| +1V8 any net | 1.8V | LDO running |
| 3.3_VCC any net | 3.3V | LDO running |
| JPROG1 Pin5 (VTARG) | 1.8V | Battery connected |
| U4_CHAR1 Pin3 (VDD) | 5.0V | USB plugged in |
| U4_CHAR1 Pin5 (STAT) | ~0V | Actively charging |
| U4_CHAR1 Pin5 (STAT) | Hi-Z/floating | Charge complete |
| JPROG1 Pin3 (XRES_N) | 1.8V | PSoC6 running |
| SDA / SCL (idle) | 1.8V | I2C bus idle high |

---

## V2 Design Notes

- **UART debug header:** Break out P5_1 (TX), P5_0 (RX), and GND to a 3-pin 1.27mm header. P5_1 and P5_0 are currently no-connects on the CYBLE module (pins 40 and 39). Without this header, getting serial output requires soldering directly to the 0.5mm castellated pad on the module.

- **Skin temperature:** Add **TMP117MAIDRVR** (DSBGA-8) to the shared I2C bus. Runs natively at 1.8V (1.8V–5.5V supply), no level shifter needed. Default I2C address 0x48. One register read (0x00) returns 16-bit temp; multiply by 0.0078125 for °C.
- **Charge rate for 60mAh battery:** Replace R_PROG1 with 33kΩ for 30mA (0.5C, ~2 hr). Current 47kΩ gives 21mA (~3 hr). **33kΩ is the minimum safe value — do not go lower.** Battery max charge current is 30mA; 20kΩ (50mA) and 22kΩ (45mA) both exceed this and risk heat, lithium plating, and swelling. Rule: R_PROG1 ≥ 33kΩ for this cell.
- **Do not replace CYBLE module with bare CY8C6347BZI-BLD53 on prototype:** 0.4mm WLCSP requires stencil reflow, RF matching network design, and custom FCC certification. The module handles all of this pre-certified.

# WatchWise iOS App

A native SwiftUI + CoreBluetooth app that connects to the WatchWise PSoC6
wearable over BLE and displays live **heart rate, SpO₂, step count, and
battery** — a replacement for nRF Connect.

## Requirements

- **Xcode 16+** (the Mac currently has only Command Line Tools — install the
  full **Xcode** from the Mac App Store before building).
- An iPhone running **iOS 17+** (BLE does not work in the iOS Simulator, so you
  must run on a real device).
- A free Apple ID is enough to run on your own phone (7-day signing).

## Build & run

1. Open the project:
   ```bash
   open mobile/WatchWiseApp/WatchWiseApp.xcodeproj
   ```
2. Select the **WatchWiseApp** scheme and your iPhone as the run destination
   (plug it in / pair over Wi-Fi).
3. Set your signing team: select the project → **Signing & Capabilities** →
   check *Automatically manage signing* → pick your Apple ID team. (You can
   change the bundle id from `com.watchwise.WatchWiseApp` if it’s taken.)
4. Press **⌘R** to build and run on the phone.
5. On first launch tap **Allow** for the Bluetooth permission prompt. The app
   auto-scans, finds `WatchWise2`, connects, and starts streaming.

## How it maps to the firmware

The UUIDs and data formats in `BLEManager.swift` mirror the firmware GATT
database (`task_ble.c` / `cycfg_gatt_db.h`):

| Metric | Service UUID | Characteristic UUID | Encoding |
|--------|--------------|---------------------|----------|
| Heart Rate | `180D` | `2A37` | `[flags, bpm]`, BPM = byte 1 |
| SpO₂ | `5E021176-5E66-483F-AE53-D3291D2CC787` | `F1F72B4B-1D76-43A3-ADB2-0E04905ACC8F` | uint8 % |
| Steps | (same custom service) | `C36072C1-2706-41F1-8D28-E0619FE31FFA` | uint32 little-endian |
| Battery | `180F` | `2A19` | uint8 % |

The watch advertises only its name (`WatchWise2`) with no service UUIDs, so the
app scans with `services: nil` and matches any peripheral whose advertised name
starts with `WatchWise`. The firmware pushes notifications every 2 s.

## Project layout

```
WatchWiseApp/
  WatchWiseApp.swift   App entry; owns the shared BLEManager
  BLEManager.swift     CoreBluetooth scan/connect/subscribe + decoding
  ContentView.swift    Dashboard UI (metric cards + HR sparkline)
  Assets.xcassets/     App icon + accent color placeholders
```

## Troubleshooting

- **Doesn’t find the watch:** make sure the firmware is flashed and advertising
  (it stops advertising ~2 min after boot if not connected, but the firmware
  re-arms it). Confirm it shows up in nRF Connect as `WatchWise2`.
- **Only one phone at a time:** disconnect nRF Connect / other phones first —
  the firmware accepts a single connection.
- **Values show “—”:** they populate on the first notification (≤2 s) after the
  characteristics are discovered.

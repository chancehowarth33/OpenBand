//
//  BLEManager.swift
//  WatchWiseApp
//
//  CoreBluetooth client for the WatchWise PSoC6 wearable.
//
//  Flow: scan -> show discovered devices -> user taps one -> connect ->
//  discover the four data characteristics -> subscribe to notifications.
//
//  DEMO MODE: the iOS Simulator has no Bluetooth radio, so when built for the
//  Simulator this class bypasses CoreBluetooth entirely and fabricates a fake
//  "WatchWise2" device plus realistic live data, so the full UI flow can be
//  exercised on a laptop. On a real device it uses real BLE.
//
//  GATT layout (must match firmware task_ble.c / cycfg_gatt_db):
//    Heart Rate Service          0x180D
//      Heart Rate Measurement    0x2A37   notify   [flags(1), bpm(1)]  bpm = byte[1]
//    WatchWise Health (custom)   5E021176-5E66-483F-AE53-D3291D2CC787
//      SpO2                      F1F72B4B-1D76-43A3-ADB2-0E04905ACC8F  notify  uint8 %
//      Step Count                C36072C1-2706-41F1-8D28-E0619FE31FFA  notify  uint32 LE
//    Battery Service             0x180F
//      Battery Level             0x2A19   notify   uint8 %
//

import Foundation
import CoreBluetooth

// MARK: - UUID constants

enum WW {
    static let deviceNamePrefix = "WatchWise"

    static let heartRateService   = CBUUID(string: "180D")
    static let heartRateMeasure   = CBUUID(string: "2A37")

    static let batteryService     = CBUUID(string: "180F")
    static let batteryLevel       = CBUUID(string: "2A19")

    static let healthService      = CBUUID(string: "5E021176-5E66-483F-AE53-D3291D2CC787")
    static let spo2Char           = CBUUID(string: "F1F72B4B-1D76-43A3-ADB2-0E04905ACC8F")
    static let stepsChar          = CBUUID(string: "C36072C1-2706-41F1-8D28-E0619FE31FFA")

    static let servicesToDiscover = [heartRateService, healthService, batteryService]
    static let charsToDiscover    = [heartRateMeasure, spo2Char, stepsChar, batteryLevel]
}

// MARK: - Discovered device model

struct DiscoveredDevice: Identifiable, Equatable {
    let id: UUID            // CBPeripheral.identifier (stable per device/host)
    var name: String
    var rssi: Int
    var isWatchWise: Bool
    var peripheral: CBPeripheral?   // nil in demo mode

    static func == (a: DiscoveredDevice, b: DiscoveredDevice) -> Bool { a.id == b.id }
}

// MARK: - Connection state

enum ConnectionState: Equatable {
    case poweredOff          // Bluetooth radio off / unauthorized
    case idle                // ready, not scanning
    case scanning            // looking for devices
    case connecting          // establishing link
    case connected           // discovering services / streaming data
}

// MARK: - BLE manager

final class BLEManager: NSObject, ObservableObject {

    // Live values (nil = "not received yet" / "—")
    @Published var heartRate: Int?
    @Published var spo2: Int?
    @Published var steps: Int?
    @Published var battery: Int?

    @Published var state: ConnectionState = .idle
    @Published var statusMessage = "Ready"
    @Published var lastUpdate: Date?

    // Scan results, shown in the device picker.
    @Published var devices: [DiscoveredDevice] = []
    @Published var connectedDeviceName: String?

    /// When on, automatically connect to a WatchWise (or last-used) device as
    /// soon as it appears in a scan. Persisted across launches.
    @Published var autoConnect: Bool {
        didSet { UserDefaults.standard.set(autoConnect, forKey: Self.kAutoConnect) }
    }
    /// Name of the most recently connected device (shown as a hint in the picker).
    @Published private(set) var lastDeviceName: String?

    // Short rolling history for the heart-rate sparkline (most recent last)
    @Published var hrHistory: [Int] = []
    private let hrHistoryMax = 60

    private var central: CBCentralManager!
    private var watch: CBPeripheral?

    #if targetEnvironment(simulator)
    let isDemo = true
    #else
    let isDemo = false
    #endif
    private var demoTimer: Timer?
    private var demoSteps = 3120
    private var demoBattery = 87
    private var demoHR = 72.0

    // Persistence for auto-connect / last device.
    private var lastDeviceID: UUID?
    private var autoConnectAttempted = false
    private static let kAutoConnect = "ww.autoConnect"
    private static let kLastID      = "ww.lastDeviceID"
    private static let kLastName    = "ww.lastDeviceName"

    override init() {
        let ud = UserDefaults.standard
        autoConnect = (ud.object(forKey: Self.kAutoConnect) as? Bool) ?? true
        lastDeviceName = ud.string(forKey: Self.kLastName)
        if let s = ud.string(forKey: Self.kLastID) { lastDeviceID = UUID(uuidString: s) }
        super.init()
        if isDemo {
            state = .idle
            statusMessage = "Demo mode — Simulator has no Bluetooth"
        } else {
            central = CBCentralManager(delegate: self, queue: .main)
        }
    }

    // MARK: - User actions

    func startScan() {
        if isDemo { startDemoScan(); return }
        guard central.state == .poweredOn else {
            statusMessage = "Bluetooth is off"
            return
        }
        devices.removeAll()
        autoConnectAttempted = false
        state = .scanning
        statusMessage = "Scanning…"
        // services: nil because the watch does not advertise its service UUIDs.
        central.scanForPeripherals(withServices: nil,
                                   options: [CBCentralManagerScanOptionAllowDuplicatesKey: true])
    }

    func connect(to device: DiscoveredDevice) {
        if isDemo { connectDemo(device); return }
        guard let p = device.peripheral else { return }
        central.stopScan()
        watch = p
        p.delegate = self
        state = .connecting
        statusMessage = "Connecting to \(device.name)…"
        central.connect(p, options: nil)
    }

    func disconnect() {
        if isDemo {
            demoTimer?.invalidate(); demoTimer = nil
            connectedDeviceName = nil
            state = .idle
            statusMessage = "Disconnected"
            resetValues()
            return
        }
        if let w = watch { central.cancelPeripheralConnection(w) }
        central.stopScan()
        watch = nil
        connectedDeviceName = nil
        state = central.state == .poweredOn ? .idle : .poweredOff
        statusMessage = "Disconnected"
        resetValues()
    }

    private func resetValues() {
        heartRate = nil; spo2 = nil; steps = nil; battery = nil
        hrHistory = []; lastUpdate = nil
    }

    // Remember a device so we can auto-reconnect to it next launch.
    private func rememberDevice(id: UUID, name: String?) {
        lastDeviceID = id
        lastDeviceName = name
        let ud = UserDefaults.standard
        ud.set(id.uuidString, forKey: Self.kLastID)
        ud.set(name, forKey: Self.kLastName)
    }

    // Auto-connect to a WatchWise (or the remembered) device, once per scan.
    private func maybeAutoConnect(_ d: DiscoveredDevice) {
        guard autoConnect, state == .scanning, !autoConnectAttempted else { return }
        let isTarget = d.isWatchWise || (lastDeviceID != nil && d.id == lastDeviceID)
        guard isTarget else { return }
        autoConnectAttempted = true
        statusMessage = "Auto-connecting to \(d.name)…"
        connect(to: d)
    }

    // Insert/update a discovered device, keeping WatchWise devices on top.
    private func upsert(_ d: DiscoveredDevice) {
        if let i = devices.firstIndex(where: { $0.id == d.id }) {
            devices[i].rssi = d.rssi
            devices[i].name = d.name
        } else {
            devices.append(d)
        }
        devices.sort {
            if $0.isWatchWise != $1.isWatchWise { return $0.isWatchWise }
            return $0.rssi > $1.rssi
        }
    }

    // MARK: - Demo mode

    private func startDemoScan() {
        devices.removeAll()
        autoConnectAttempted = false
        state = .scanning
        statusMessage = "Scanning… (demo)"
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) { [weak self] in
            guard let self, self.state == .scanning else { return }
            let watch = DiscoveredDevice(id: UUID(), name: "WatchWise2",
                                         rssi: -47, isWatchWise: true, peripheral: nil)
            self.upsert(watch)
            self.upsert(DiscoveredDevice(id: UUID(), name: "AirPods Pro",
                                         rssi: -63, isWatchWise: false, peripheral: nil))
            self.upsert(DiscoveredDevice(id: UUID(), name: "Living Room TV",
                                         rssi: -80, isWatchWise: false, peripheral: nil))
            self.statusMessage = "Tap WatchWise2 to connect (demo)"
            self.maybeAutoConnect(watch)
        }
    }

    private func connectDemo(_ device: DiscoveredDevice) {
        state = .connecting
        statusMessage = "Connecting to \(device.name)… (demo)"
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.8) { [weak self] in
            guard let self else { return }
            self.connectedDeviceName = device.name
            self.rememberDevice(id: device.id, name: device.name)
            self.state = .connected
            self.statusMessage = "Streaming live data (demo)"
            self.demoTimer = Timer.scheduledTimer(withTimeInterval: 1.0,
                                                  repeats: true) { [weak self] _ in
                self?.tickDemo()
            }
            self.tickDemo()
        }
    }

    private func tickDemo() {
        // Heart rate: smooth random walk in 58–105.
        demoHR += Double.random(in: -4...4)
        demoHR = min(max(demoHR, 58), 105)
        let bpm = Int(demoHR.rounded())
        heartRate = bpm
        hrHistory.append(bpm)
        if hrHistory.count > hrHistoryMax { hrHistory.removeFirst() }

        spo2 = Int.random(in: 96...99)
        demoSteps += Int.random(in: 0...6)
        steps = demoSteps
        if demoBattery > 1 && Int.random(in: 0...8) == 0 { demoBattery -= 1 }
        battery = demoBattery
        lastUpdate = Date()
    }
}

// MARK: - CBCentralManagerDelegate

extension BLEManager: CBCentralManagerDelegate {

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            state = .idle
            statusMessage = "Bluetooth ready"
            startScan()
        case .poweredOff:
            state = .poweredOff
            statusMessage = "Turn on Bluetooth"
            resetValues()
        case .unauthorized:
            state = .poweredOff
            statusMessage = "Allow Bluetooth in Settings"
        case .unsupported:
            state = .poweredOff
            statusMessage = "BLE not supported on this device"
        default:
            state = .poweredOff
            statusMessage = "Bluetooth unavailable"
        }
    }

    func centralManager(_ central: CBCentralManager,
                        didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any],
                        rssi RSSI: NSNumber) {
        let advName = (advertisementData[CBAdvertisementDataLocalNameKey] as? String)
            ?? peripheral.name
        guard let name = advName, !name.isEmpty else { return }   // skip unnamed noise

        let d = DiscoveredDevice(id: peripheral.identifier,
                                 name: name,
                                 rssi: RSSI.intValue,
                                 isWatchWise: name.hasPrefix(WW.deviceNamePrefix),
                                 peripheral: peripheral)
        upsert(d)
        maybeAutoConnect(d)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        state = .connected
        connectedDeviceName = peripheral.name
        rememberDevice(id: peripheral.identifier, name: peripheral.name)
        statusMessage = "Connected — discovering services…"
        resetValues()
        peripheral.discoverServices(WW.servicesToDiscover)
    }

    func centralManager(_ central: CBCentralManager,
                        didFailToConnect peripheral: CBPeripheral, error: Error?) {
        statusMessage = "Connect failed: \(error?.localizedDescription ?? "unknown")"
        state = .idle
        startScan()
    }

    func centralManager(_ central: CBCentralManager,
                        didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        connectedDeviceName = nil
        statusMessage = "Lost connection"
        resetValues()
        state = .idle
        startScan()
    }
}

// MARK: - CBPeripheralDelegate

extension BLEManager: CBPeripheralDelegate {

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard error == nil, let services = peripheral.services else {
            statusMessage = "Service discovery failed"; return
        }
        for service in services {
            peripheral.discoverCharacteristics(WW.charsToDiscover, for: service)
        }
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        guard let chars = service.characteristics else { return }
        for c in chars where WW.charsToDiscover.contains(c.uuid) {
            peripheral.setNotifyValue(true, for: c)
            peripheral.readValue(for: c)
        }
        statusMessage = "Streaming live data"
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard error == nil, let data = characteristic.value else { return }
        decode(uuid: characteristic.uuid, data: data)
    }

    private func decode(uuid: CBUUID, data: Data) {
        let bytes = [UInt8](data)
        guard !bytes.isEmpty else { return }

        switch uuid {
        case WW.heartRateMeasure:
            let flags = bytes[0]
            let bpm: Int
            if flags & 0x01 == 0 {
                bpm = bytes.count >= 2 ? Int(bytes[1]) : 0
            } else {
                bpm = bytes.count >= 3 ? Int(bytes[1]) | (Int(bytes[2]) << 8) : 0
            }
            heartRate = bpm
            if bpm > 0 {
                hrHistory.append(bpm)
                if hrHistory.count > hrHistoryMax { hrHistory.removeFirst() }
            }

        case WW.spo2Char:
            spo2 = Int(bytes[0])

        case WW.stepsChar:
            var v: UInt32 = 0
            for (i, b) in bytes.prefix(4).enumerated() { v |= UInt32(b) << (8 * i) }
            steps = Int(v)

        case WW.batteryLevel:
            battery = Int(bytes[0])

        default:
            return
        }
        lastUpdate = Date()
    }
}

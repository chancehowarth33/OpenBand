//
//  ContentView.swift
//  WatchWiseApp
//
//  Two screens: a device picker (ScanView) and, once connected, the live
//  dashboard (DashboardView). The root swaps between them based on BLE state.
//

import SwiftUI

struct ContentView: View {
    @EnvironmentObject var ble: BLEManager

    var body: some View {
        ZStack {
            if ble.state == .connected {
                DashboardView()
                    .transition(.move(edge: .trailing).combined(with: .opacity))
            } else {
                ScanView()
                    .transition(.move(edge: .leading).combined(with: .opacity))
            }
        }
        .animation(.snappy, value: ble.state == .connected)
    }
}

// MARK: - Device picker

struct ScanView: View {
    @EnvironmentObject var ble: BLEManager

    var body: some View {
        NavigationStack {
            List {
                Section {
                    HStack(spacing: 12) {
                        statusDot
                        Text(ble.statusMessage)
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                    }
                    Toggle("Auto-connect to WatchWise", isOn: $ble.autoConnect)
                    if let last = ble.lastDeviceName {
                        HStack {
                            Text("Last device").foregroundStyle(.secondary)
                            Spacer()
                            Text(last).foregroundStyle(.secondary)
                        }
                        .font(.subheadline)
                    }
                }

                Section("Nearby Devices") {
                    if ble.devices.isEmpty {
                        HStack(spacing: 10) {
                            ProgressView()
                            Text(ble.isDemo ? "Looking for demo devices…"
                                            : "Scanning for devices…")
                                .foregroundStyle(.secondary)
                        }
                        .padding(.vertical, 4)
                    } else {
                        ForEach(ble.devices) { device in
                            Button { ble.connect(to: device) } label: {
                                DeviceRow(device: device,
                                          connecting: ble.state == .connecting)
                            }
                            .disabled(ble.state == .connecting)
                        }
                    }
                }
            }
            .navigationTitle("WatchWise")
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button { ble.startScan() } label: {
                        Image(systemName: "arrow.clockwise")
                    }
                    .disabled(ble.state == .connecting)
                }
            }
            .onAppear {
                if ble.devices.isEmpty { ble.startScan() }
            }
        }
    }

    private var statusDot: some View {
        Circle()
            .fill(ble.state == .scanning ? .orange
                  : ble.state == .connecting ? .orange
                  : ble.state == .poweredOff ? .red : .gray)
            .frame(width: 10, height: 10)
    }
}

struct DeviceRow: View {
    let device: DiscoveredDevice
    let connecting: Bool

    var body: some View {
        HStack(spacing: 14) {
            Image(systemName: device.isWatchWise ? "applewatch" : "dot.radiowaves.left.and.right")
                .font(.title3)
                .foregroundStyle(device.isWatchWise ? Color.green : .secondary)
                .frame(width: 28)

            VStack(alignment: .leading, spacing: 2) {
                Text(device.name)
                    .font(.body)
                    .foregroundStyle(.primary)
                Text(device.isWatchWise ? "WatchWise wearable" : "Other device")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            Spacer()

            SignalBars(rssi: device.rssi)

            Image(systemName: "chevron.right")
                .font(.caption)
                .foregroundStyle(.tertiary)
        }
        .padding(.vertical, 4)
    }
}

// Simple 4-bar RSSI strength indicator.
struct SignalBars: View {
    let rssi: Int

    private var level: Int {   // 0...4
        switch rssi {
        case (-55)...:   return 4
        case (-67)...:   return 3
        case (-78)...:   return 2
        case (-90)...:   return 1
        default:         return 0
        }
    }

    var body: some View {
        HStack(alignment: .bottom, spacing: 2) {
            ForEach(1...4, id: \.self) { i in
                RoundedRectangle(cornerRadius: 1)
                    .fill(i <= level ? Color.green : Color.gray.opacity(0.3))
                    .frame(width: 4, height: CGFloat(5 + i * 3))
            }
        }
    }
}

// MARK: - Dashboard

struct DashboardView: View {
    @EnvironmentObject var ble: BLEManager

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 16) {
                    ConnectedBanner()

                    LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())],
                              spacing: 16) {
                        MetricCard(title: "Heart Rate", icon: "heart.fill", tint: .red,
                                   value: ble.heartRate.map(String.init) ?? "—", unit: "BPM")
                        MetricCard(title: "SpO₂", icon: "lungs.fill", tint: .blue,
                                   value: ble.spo2.map(String.init) ?? "—", unit: "%")
                        MetricCard(title: "Steps", icon: "figure.walk", tint: .green,
                                   value: ble.steps.map { $0.formatted() } ?? "—", unit: "today")
                        MetricCard(title: "Battery", icon: batteryIcon, tint: batteryTint,
                                   value: ble.battery.map(String.init) ?? "—", unit: "%")
                    }

                    if !ble.hrHistory.isEmpty {
                        HeartRateChart(samples: ble.hrHistory)
                    }

                    if let last = ble.lastUpdate {
                        Text("Updated \(last.formatted(date: .omitted, time: .standard))")
                            .font(.footnote).foregroundStyle(.secondary)
                    }
                }
                .padding()
            }
            .navigationTitle(ble.connectedDeviceName ?? "WatchWise")
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button("Disconnect", role: .destructive) { ble.disconnect() }
                }
            }
        }
    }

    private var batteryIcon: String {
        guard let b = ble.battery else { return "battery.0percent" }
        switch b {
        case ..<13: return "battery.0percent"
        case ..<38: return "battery.25percent"
        case ..<63: return "battery.50percent"
        case ..<88: return "battery.75percent"
        default:    return "battery.100percent"
        }
    }

    private var batteryTint: Color {
        guard let b = ble.battery else { return .gray }
        return b < 20 ? .red : .green
    }
}

struct ConnectedBanner: View {
    @EnvironmentObject var ble: BLEManager

    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: "antenna.radiowaves.left.and.right")
                .foregroundStyle(.green)
            VStack(alignment: .leading, spacing: 2) {
                Text("Connected\(ble.isDemo ? " (demo)" : "")")
                    .font(.subheadline).bold()
                Text(ble.statusMessage)
                    .font(.caption).foregroundStyle(.secondary)
            }
            Spacer()
        }
        .padding()
        .background(.thinMaterial, in: RoundedRectangle(cornerRadius: 14))
    }
}

// MARK: - Reusable pieces

struct MetricCard: View {
    let title: String
    let icon: String
    let tint: Color
    let value: String
    let unit: String

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Image(systemName: icon).foregroundStyle(tint)
                Text(title).font(.subheadline).foregroundStyle(.secondary)
                Spacer()
            }
            HStack(alignment: .firstTextBaseline, spacing: 4) {
                Text(value)
                    .font(.system(size: 40, weight: .bold, design: .rounded))
                    .contentTransition(.numericText())
                Text(unit).font(.caption).foregroundStyle(.secondary)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding()
        .background(.thinMaterial, in: RoundedRectangle(cornerRadius: 16))
    }
}

struct HeartRateChart: View {
    let samples: [Int]

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Heart Rate Trend")
                .font(.subheadline).foregroundStyle(.secondary)
            GeometryReader { geo in
                let lo = Double(samples.min() ?? 0)
                let hi = Double(samples.max() ?? 1)
                let span = max(hi - lo, 1)
                Path { path in
                    for (i, s) in samples.enumerated() {
                        let x = samples.count <= 1 ? 0 :
                            geo.size.width * Double(i) / Double(samples.count - 1)
                        let y = geo.size.height * (1 - (Double(s) - lo) / span)
                        if i == 0 { path.move(to: CGPoint(x: x, y: y)) }
                        else      { path.addLine(to: CGPoint(x: x, y: y)) }
                    }
                }
                .stroke(.red, style: StrokeStyle(lineWidth: 2, lineJoin: .round))
            }
            .frame(height: 80)
        }
        .padding()
        .background(.thinMaterial, in: RoundedRectangle(cornerRadius: 16))
    }
}

#Preview {
    ContentView().environmentObject(BLEManager())
}

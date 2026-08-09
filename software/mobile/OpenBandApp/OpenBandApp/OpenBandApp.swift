//
//  OpenBandApp.swift
//  OpenBandApp
//
//  Entry point. Creates the single shared BLEManager and injects it into
//  the SwiftUI environment so every view can observe live sensor data.
//

import SwiftUI

@main
struct OpenBandApp: App {
    @StateObject private var ble = BLEManager()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(ble)
        }
    }
}

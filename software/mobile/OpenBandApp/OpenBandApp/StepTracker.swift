//
//  StepTracker.swift
//  OpenBandApp
//
//  Option A — the app owns the calendar.
//
//  The watch reports a CUMULATIVE step count (steps since the BMI270 counter
//  started). That counter keeps climbing while connected or not, but RESETS to
//  0 whenever the watch reboots. So "today's steps" can't be a naive
//  `current - midnightBaseline`; we accumulate the per-reading delta and handle
//  three cases:
//    • delta >= 0          -> normal increment, add it
//    • delta <  0          -> the counter reset (reboot), add the new value
//    • local day changed   -> roll over: today resets to 0
//
//  State is persisted in UserDefaults keyed by the device SERIAL (from DIS), so
//  it survives app relaunch, disconnects, and tells multiple watches apart.
//
//  Limitation (inherent to Option A with a since-boot counter): "today" only
//  counts steps observed from the first reading the app sees each day — steps
//  taken before the app first connects on a given day can't be recovered.
//

import Foundation
import Combine

final class StepTracker: ObservableObject {

    /// Steps for the current local day. nil = not known yet (no serial/reading).
    @Published private(set) var today: Int?

    private var serial: String?
    private var latestCumulative: Int?

    // MARK: - Inputs (called by BLEManager)

    /// The device's serial (from the Device Information Service). Determines
    /// which persisted day-state to use.
    func setSerial(_ s: String) {
        guard s != serial, !s.isEmpty else { return }
        serial = s
        recompute()
    }

    /// A fresh cumulative step reading from the watch.
    func record(cumulative c: Int) {
        latestCumulative = c
        recompute()
    }

    /// Call on disconnect so the next session re-baselines cleanly. The
    /// persisted daily total is kept and repopulated on reconnect.
    func sessionEnded() {
        latestCumulative = nil
        today = nil
    }

    // MARK: - Core

    /// Local-time day key, e.g. "2026-6-29". Calendar.current rolls at the
    /// device's local midnight.
    private func dayKey(_ d: Date = Date()) -> String {
        let c = Calendar.current.dateComponents([.year, .month, .day], from: d)
        return "\(c.year ?? 0)-\(c.month ?? 0)-\(c.day ?? 0)"
    }

    private func recompute() {
        guard let serial, let c = latestCumulative else { return }

        let ud   = UserDefaults.standard
        let kDay  = "ww.steps.\(serial).day"
        let kTod  = "ww.steps.\(serial).today"
        let kLast = "ww.steps.\(serial).last"

        let curDay     = dayKey()
        let storedDay  = ud.string(forKey: kDay)
        var todaySteps = ud.integer(forKey: kTod)
        let last       = ud.object(forKey: kLast) as? Int

        if storedDay != curDay {
            // New day (or first ever for this device): start at 0, this reading
            // becomes the baseline — don't carry yesterday's jump.
            todaySteps = 0
        } else if let l = last {
            let delta = c - l
            todaySteps += (delta >= 0) ? delta : c   // delta < 0 => counter reset
        }

        ud.set(curDay, forKey: kDay)
        ud.set(todaySteps, forKey: kTod)
        ud.set(c, forKey: kLast)
        today = todaySteps
    }
}

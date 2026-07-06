/**
 * @file ww_rtc.h
 * @brief Wall-clock real-time clock (PSoC6 RTC, WCO-backed).
 *
 * The RTC runs off the 32.768 kHz WCO crystal and keeps time through system
 * deep sleep at ~uA — making it the foundation for both daily-stat rollover
 * (Option B) and timestamped on-device logging.
 *
 * The watch has no idea what time it is at boot. The phone app sends the current
 * LOCAL epoch on connect (ww_rtc_set_epoch), so the RTC holds local time and
 * rolls over at the user's local midnight. A full power loss (dead battery)
 * loses the time until the app re-sets it.
 */
#ifndef WW_RTC_H
#define WW_RTC_H

#include <stdint.h>
#include <stdbool.h>

/* Initialize the RTC. Call once at startup (before tasks). */
void ww_rtc_init(void);

/* Set the wall clock from a Unix epoch (seconds since 1970). The app sends
 * LOCAL epoch, so the RTC holds local time. */
void ww_rtc_set_epoch(uint32_t epoch);

/* Current time as a Unix epoch (seconds), or 0 if the RTC isn't ready. */
uint32_t ww_rtc_get_epoch(void);

/* True once the app has set the time at least once this power cycle. */
bool ww_rtc_is_set(void);

/* Day number (days since 1970) in the stored local time — increments at local
 * midnight. Compare across readings to detect a new day for daily stats. */
uint32_t ww_rtc_day_number(void);

#endif /* WW_RTC_H */

/**
 * @file ww_rtc.c
 * @brief PSoC6 RTC wall clock (WCO-backed). See ww_rtc.h.
 *
 * Epoch <-> calendar conversion is done with a portable civil-day algorithm
 * (Howard Hinnant's days_from_civil / civil_from_days) so there is no
 * dependency on newlib's timezone handling — the app provides local epoch and
 * we store/return it verbatim.
 */
#include "ww_rtc.h"
#include "cyhal.h"
#include <time.h>
#include <stdio.h>

static cyhal_rtc_t rtc;
static bool        rtc_ready = false;
static bool        time_set  = false;

/* ── Portable civil <-> days-since-1970 (no timezone) ─────────────────── */

static long days_from_civil(long y, unsigned m, unsigned d)
{
    y -= (m <= 2);
    long     era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097L + (long)doe - 719468L;
}

static void civil_from_days(long z, int *y, unsigned *m, unsigned *d)
{
    z += 719468L;
    long     era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097L);
    unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    long     yy  = (long)yoe + era * 400L;
    unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    unsigned mp  = (5u * doy + 2u) / 153u;
    *d = doy - (153u * mp + 2u) / 5u + 1u;
    *m = mp + (mp < 10u ? 3u : (unsigned)-9);
    *y = (int)(yy + (*m <= 2));
}

static uint32_t tm_to_epoch(const struct tm *t)
{
    long days = days_from_civil(t->tm_year + 1900, (unsigned)t->tm_mon + 1, (unsigned)t->tm_mday);
    return (uint32_t)(((days * 24L + t->tm_hour) * 60L + t->tm_min) * 60L + t->tm_sec);
}

static void epoch_to_tm(uint32_t e, struct tm *t)
{
    long     days = (long)(e / 86400u);
    unsigned secs = (unsigned)(e % 86400u);
    int      y;
    unsigned m, d;
    civil_from_days(days, &y, &m, &d);
    t->tm_year = y - 1900;
    t->tm_mon  = (int)m - 1;
    t->tm_mday = (int)d;
    t->tm_hour = (int)(secs / 3600u);
    t->tm_min  = (int)((secs % 3600u) / 60u);
    t->tm_sec  = (int)(secs % 60u);
    long wd = (days + 4) % 7;                 /* 1970-01-01 was Thursday (4) */
    if (wd < 0) wd += 7;
    t->tm_wday = (int)wd;
    t->tm_yday = 0;
    t->tm_isdst = 0;
}

/* ── Public API ───────────────────────────────────────────────────────── */

void ww_rtc_init(void)
{
    if (cyhal_rtc_init(&rtc) == CY_RSLT_SUCCESS) {
        rtc_ready = true;
        printf("[RTC] initialized (WCO)\n");
    } else {
        printf("[RTC] init FAILED\n");
    }
}

void ww_rtc_set_epoch(uint32_t epoch)
{
    if (!rtc_ready) return;
    struct tm t;
    epoch_to_tm(epoch, &t);
    if (cyhal_rtc_write(&rtc, &t) == CY_RSLT_SUCCESS) {
        time_set = true;
        printf("[RTC] time set: %04d-%02d-%02d %02d:%02d:%02d\n",
               t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
               t.tm_hour, t.tm_min, t.tm_sec);
    }
}

uint32_t ww_rtc_get_epoch(void)
{
    if (!rtc_ready) return 0;
    struct tm t;
    if (cyhal_rtc_read(&rtc, &t) != CY_RSLT_SUCCESS) return 0;
    return tm_to_epoch(&t);
}

bool ww_rtc_is_set(void)
{
    return time_set;
}

uint32_t ww_rtc_day_number(void)
{
    return ww_rtc_get_epoch() / 86400u;
}

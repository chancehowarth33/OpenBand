/**
 * @file health_algo.c
 * @brief Heart rate and SpO2 algorithm (PPG)
 *
 * Heart rate + SpO2
 * -----------------
 * Maintains a 100-sample (4 s at 25 Hz) ring buffer.  Recomputes once/second:
 *   1. Remove DC mean → AC component.
 *   2. Smooth AC with a 5-sample moving average to reduce sample-to-sample noise.
 *   3. Threshold = 0.35 × max positive AC peak (adapts to actual signal height).
 *   4. Find peaks; need ≥ 2 to compute an RR interval.
 *   5. EMA (α = 0.25) so the reported BPM drifts gradually rather than jumping.
 *
 * "No finger" is only declared when the IR DC mean stays below FINGER_THRESH.
 * If the finger is on but the signal is weak, the last valid reading is held.
 */
#include "health_algo.h"
#include <math.h>
#include <stdbool.h>

/* ═══════════════════════════════════════════════════════════════════════
 * Tuneable constants
 * ═══════════════════════════════════════════════════════════════════════ */
#define PPG_N           100     /* ring buffer: 4 s at 25 Hz effective rate  */
#define FINGER_THRESH   20000   /* IR DC below this → no finger              */
#define MIN_PEAK_GAP    8       /* min samples between peaks  (~0.32 s)      */
#define COMPUTE_EVERY   25      /* recompute interval (25 samples = 1 s)     */
#define EMA_ALPHA       0.25f   /* weight given to newest HR sample          */
#define SMOOTH_WINDOW   5       /* moving-average kernel width for AC signal */

/* ── Buffers (all static → BSS, never on any task stack) ─────────────── */
static uint32_t ir_buf [PPG_N];
static uint32_t red_buf[PPG_N];
static float    ac_ir  [PPG_N];
static float    ac_red [PPG_N];
static float    smooth [PPG_N];   /* smoothed AC IR for peak detection        */

static uint16_t ppg_head      = 0;
static bool     ppg_full      = false;
static uint8_t  compute_timer = 0;

static float hr_ema   = 0.0f;
static float spo2_ema = 0.0f;

static float last_perfusion = 0.0f;   /* AC/DC of IR, % — signal-quality metric */
static int   spo2_enabled   = 1;      /* scheduler disables during IR-only HR    */

volatile uint8_t  health_hr    = 0;
volatile uint8_t  health_spo2  = 0;
volatile uint32_t health_steps = 0;   /* written by task_bmi270 hardware counter */

/* ── Core computation ─────────────────────────────────────────────────── */

static void compute_hr_spo2(void)
{
    /* ── DC means ──────────────────────────────────────────────────── */
    float dc_ir = 0.0f, dc_red = 0.0f;
    for (int i = 0; i < PPG_N; i++) {
        dc_ir  += (float)ir_buf[i];
        dc_red += (float)red_buf[i];
    }
    dc_ir  /= PPG_N;
    dc_red /= PPG_N;

    /* No finger — clear results and bail */
    if (dc_ir < (float)FINGER_THRESH) {
        health_hr   = 0;
        health_spo2 = 0;
        hr_ema      = 0.0f;
        spo2_ema    = 0.0f;
        return;
    }

    /* ── AC signals ────────────────────────────────────────────────── */
    float ac_ir_sq = 0.0f, ac_red_sq = 0.0f;
    for (int i = 0; i < PPG_N; i++) {
        ac_ir[i]   = (float)ir_buf[i]  - dc_ir;
        ac_red[i]  = (float)red_buf[i] - dc_red;
        ac_ir_sq  += ac_ir[i]  * ac_ir[i];
        ac_red_sq += ac_red[i] * ac_red[i];
    }
    float ac_ir_rms  = sqrtf(ac_ir_sq  / PPG_N);
    float ac_red_rms = sqrtf(ac_red_sq / PPG_N);

    /* Perfusion index = AC/DC of IR, in %.  Signal-quality metric used for
     * adaptive LED current and to gate weak/noisy readings. */
    last_perfusion = (dc_ir > 1.0f) ? (ac_ir_rms / dc_ir) * 100.0f : 0.0f;

    /* ── SpO2 (only when enabled — needs the Red LED on) ───────────── */
    if (spo2_enabled && ac_ir_rms > 1.0f) {
        float R    = (ac_red_rms / dc_red) / (ac_ir_rms / dc_ir);
        float spo2 = -45.060f * R * R + 30.354f * R + 94.845f;
        if      (spo2 > 100.0f) spo2 = 100.0f;
        else if (spo2 <  70.0f) spo2 =   0.0f;

        if (spo2 > 0.0f) {
            spo2_ema    = (spo2_ema < 1.0f) ? spo2
                        : (EMA_ALPHA * spo2 + (1.0f - EMA_ALPHA) * spo2_ema);
            health_spo2 = (uint8_t)(spo2_ema + 0.5f);
        }
    }

    /* ── 5-sample moving average on AC IR to reduce noise ─────────── */
    int half = SMOOTH_WINDOW / 2;
    for (int i = 0; i < PPG_N; i++) {
        float sum = 0.0f;
        int   cnt = 0;
        for (int k = i - half; k <= i + half; k++) {
            if (k >= 0 && k < PPG_N) { sum += ac_ir[k]; cnt++; }
        }
        smooth[i] = sum / cnt;
    }

    /* ── Frequency-domain HR via autocorrelation ───────────────────
     * Find the lag (heartbeat period) with the strongest self-similarity. At
     * 25 Hz: 40 bpm = 37.5 samples, 200 bpm = 7.5 samples. Far more robust to
     * noise than single-peak timing (the cheap stand-in for an FFT). */
    float e0 = 0.0f;
    for (int i = 0; i < PPG_N; i++) e0 += smooth[i] * smooth[i];

    float best_corr = 0.0f;
    int   best_lag  = 0;
    for (int lag = 8; lag <= 37; lag++) {
        float c = 0.0f;
        for (int i = 0; i + lag < PPG_N; i++) c += smooth[i] * smooth[i + lag];
        if (c > best_corr) { best_corr = c; best_lag = lag; }
    }
    float ac_quality = (e0 > 1.0f && best_lag > 0) ? (best_corr / e0) : 0.0f;
    float hr_ac      = (best_lag > 0) ? (1500.0f / (float)best_lag) : 0.0f; /* 60*25/lag */

    /* ── Time-domain HR via peak detection (fallback / cross-check) ── */
    float peak_max = 0.0f;
    for (int i = 0; i < PPG_N; i++)
        if (smooth[i] > peak_max) peak_max = smooth[i];

    float hr_peak = 0.0f;
    if (peak_max >= 1.0f) {
        float threshold = 0.35f * peak_max;
        bool  above   = false;
        float seg_max = 0.0f;
        int   seg_idx = 0;
        int   peaks[20];
        int   n_peaks = 0;
        for (int i = 0; i < PPG_N && n_peaks < 20; i++) {
            if (smooth[i] > threshold) {
                if (!above) { above = true; seg_max = smooth[i]; seg_idx = i; }
                else if (smooth[i] > seg_max) { seg_max = smooth[i]; seg_idx = i; }
            } else if (above) {
                above = false;
                if (n_peaks == 0 || (seg_idx - peaks[n_peaks - 1]) >= MIN_PEAK_GAP)
                    peaks[n_peaks++] = seg_idx;
            }
        }
        if (n_peaks >= 2) {
            float avg_rr = (float)(peaks[n_peaks - 1] - peaks[0]) / (float)(n_peaks - 1);
            if (avg_rr > 0.0f) hr_peak = 1500.0f / avg_rr;
        }
    }

    /* ── Pick the best estimate, quality-gated ──────────────────────
     * Trust the autocorrelation HR when it is confident; otherwise fall back to
     * the peak detector. Require a minimum perfusion so a weak or motion-
     * corrupted window holds the previous reading instead of reporting noise. */
    float hr_est = 0.0f;
    if (last_perfusion >= 0.05f) {
        if (ac_quality > 0.35f && hr_ac >= 40.0f && hr_ac <= 200.0f)
            hr_est = hr_ac;
        else if (hr_peak >= 40.0f && hr_peak <= 200.0f)
            hr_est = hr_peak;
    }

    if (hr_est >= 40.0f && hr_est <= 200.0f) {
        hr_ema    = (hr_ema < 1.0f) ? hr_est
                  : (EMA_ALPHA * hr_est + (1.0f - EMA_ALPHA) * hr_ema);
        health_hr = (uint8_t)(hr_ema + 0.5f);
    }
    /* else: hold last value */
}

/* ── Quality / control accessors ──────────────────────────────────────── */

float health_algo_perfusion(void)
{
    return last_perfusion;
}

void health_algo_set_spo2_enabled(int enabled)
{
    spo2_enabled = enabled;
}

/* ── Public API ───────────────────────────────────────────────────────── */

void health_algo_update_ppg(uint32_t red, uint32_t ir)
{
    ir_buf [ppg_head] = ir;
    red_buf[ppg_head] = red;

    if (++ppg_head >= PPG_N) {
        ppg_head = 0;
        ppg_full = true;
    }

    if (ppg_full && ++compute_timer >= COMPUTE_EVERY) {
        compute_timer = 0;
        compute_hr_spo2();
    }
}

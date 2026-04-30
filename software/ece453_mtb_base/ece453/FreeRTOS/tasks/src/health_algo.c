/**
 * @file health_algo.c
 * @brief Health metric algorithms: heart rate, SpO2, step count
 *
 * Heart rate + SpO2
 * -----------------
 * Maintains a 100-sample (4 s at 25 Hz) ring buffer of IR and Red values.
 * Once full, recalculates once per second:
 *   1. Remove DC (mean) to get the pulsatile AC component.
 *   2. Find peaks in the AC IR signal.  Average RR interval → HR.
 *   3. SpO2 from the ratio R = (AC_red/DC_red) / (AC_ir/DC_ir)
 *      using Maxim's empirical calibration curve.
 *
 * Step counting
 * -------------
 * Computes the resultant acceleration magnitude from the IMU at 10 Hz.
 * A step is counted when magnitude rises above STEP_HIGH then falls back
 * below STEP_LOW, with a 500 ms debounce between steps.
 */
#include "health_algo.h"
#include <math.h>
#include <stdbool.h>

/* ═══════════════════════════════════════════════════════════════════════
 * PPG — heart rate + SpO2
 * ═══════════════════════════════════════════════════════════════════════ */

#define PPG_N           100    /* ring buffer length: 4 s at 25 Hz        */
#define FINGER_THRESH   50000  /* min IR DC for "finger detected"          */
#define MIN_PEAK_GAP    10     /* min samples between detected peaks       */
#define COMPUTE_EVERY   25     /* recompute once per second (25 samples)   */

/* Ring buffer — static so they live in BSS, not on any task's stack */
static uint32_t ir_buf [PPG_N];
static uint32_t red_buf[PPG_N];

/* Temporary AC arrays used only inside compute_hr_spo2 — also static */
static float ac_ir [PPG_N];
static float ac_red[PPG_N];

static uint16_t ppg_head      = 0;
static bool     ppg_full      = false;
static uint8_t  compute_timer = 0;

volatile uint8_t  health_hr   = 0;
volatile uint8_t  health_spo2 = 0;

static void compute_hr_spo2(void)
{
    /* ── DC means ─────────────────────────────────────────────────── */
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
        return;
    }

    /* ── AC signals + RMS ─────────────────────────────────────────── */
    float ac_ir_sq = 0.0f, ac_red_sq = 0.0f, ac_ir_peak = 0.0f;
    for (int i = 0; i < PPG_N; i++) {
        ac_ir[i]   = (float)ir_buf[i]  - dc_ir;
        ac_red[i]  = (float)red_buf[i] - dc_red;
        ac_ir_sq  += ac_ir[i]  * ac_ir[i];
        ac_red_sq += ac_red[i] * ac_red[i];
        if (ac_ir[i] > ac_ir_peak)
            ac_ir_peak = ac_ir[i];
    }
    float ac_ir_rms  = sqrtf(ac_ir_sq  / PPG_N);
    float ac_red_rms = sqrtf(ac_red_sq / PPG_N);

    /* ── SpO2 (Maxim AN6409 calibration curve) ────────────────────── */
    if (ac_ir_rms > 10.0f) {
        float R    = (ac_red_rms / dc_red) / (ac_ir_rms / dc_ir);
        float spo2 = -45.060f * R * R + 30.354f * R + 94.845f;
        if      (spo2 > 100.0f) spo2 = 100.0f;
        else if (spo2 <  70.0f) spo2 =   0.0f;   /* clamp implausible */
        health_spo2 = (uint8_t)spo2;
    }

    /* ── Peak detection for heart rate ───────────────────────────── */
    float threshold = 0.4f * ac_ir_peak;
    if (threshold < 50.0f)
        return;   /* signal too flat — skip HR this cycle */

    bool  above   = false;
    float seg_max = 0.0f;
    int   seg_idx = 0;

    int  peaks[20];
    int  n_peaks = 0;

    for (int i = 0; i < PPG_N && n_peaks < 20; i++) {
        if (ac_ir[i] > threshold) {
            if (!above) {
                above   = true;
                seg_max = ac_ir[i];
                seg_idx = i;
            } else if (ac_ir[i] > seg_max) {
                seg_max = ac_ir[i];
                seg_idx = i;
            }
        } else if (above) {
            above = false;
            if (n_peaks == 0 || (seg_idx - peaks[n_peaks - 1]) >= MIN_PEAK_GAP)
                peaks[n_peaks++] = seg_idx;
        }
    }

    if (n_peaks >= 2) {
        float span   = (float)(peaks[n_peaks - 1] - peaks[0]);
        float avg_rr = span / (float)(n_peaks - 1);   /* samples/beat */
        float hr     = 60.0f * 25.0f / avg_rr;        /* beats/min    */
        if (hr >= 40.0f && hr <= 200.0f)
            health_hr = (uint8_t)hr;
    }
}

void health_algo_update_ppg(uint32_t red, uint32_t ir)
{
    ir_buf [ppg_head] = ir;
    red_buf[ppg_head] = red;

    if (++ppg_head >= PPG_N) {
        ppg_head = 0;
        ppg_full = true;
    }

    /* Only compute once per second once the buffer has filled */
    if (ppg_full && ++compute_timer >= COMPUTE_EVERY) {
        compute_timer = 0;
        compute_hr_spo2();
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * IMU — step counting
 * ═══════════════════════════════════════════════════════════════════════
 *
 * At ±2g / 16-bit: 1 g ≈ 16 384 LSB.  Magnitude at rest ≈ 16 384.
 * A step swings the wrist above ~1.22 g then back below ~0.73 g.
 */
#define STEP_HIGH     20000.0f   /* ~1.22 g */
#define STEP_LOW      12000.0f   /* ~0.73 g */
#define STEP_MIN_GAP  5          /* 500 ms min between steps (5 × 100 ms) */

volatile uint32_t health_steps = 0;

void health_algo_update_imu(int16_t ax, int16_t ay, int16_t az)
{
    static bool     above       = false;
    static uint32_t sample_cnt  = 0;
    static uint32_t last_step   = 0;

    sample_cnt++;

    float mag = sqrtf((float)ax * ax + (float)ay * ay + (float)az * az);

    if (!above && mag > STEP_HIGH) {
        above = true;
    } else if (above && mag < STEP_LOW) {
        above = false;
        if ((sample_cnt - last_step) >= STEP_MIN_GAP) {
            health_steps++;
            last_step = sample_cnt;
        }
    }
}

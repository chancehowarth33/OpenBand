#!/usr/bin/env bash
# Reboot the PSoC6 over SWD WITHOUT reprogramming, so boot logs (e.g. BMI270
# init) can be captured on the serial monitor.
#
# Usage:
#   1. Open your serial monitor first (115200 baud on the KitProg3 COM port).
#   2. Hold the JPROG1 pins in contact (same as programming).
#   3. Run ./reset.sh  — it retries the acquire until it catches, then resets
#      and lets the firmware run, so you see the full boot output.
#
# Needs the JPROG contact + XRES the same way `make program` does. The wide
# srst pulse (200us) compensates for the RC filter on XRES_N (R4).

OCD=/Applications/ModusToolboxProgtools-1.8/openocd
TRIES=${1:-15}

for i in $(seq 1 "$TRIES"); do
  OUT=$("$OCD/bin/openocd" -s "$OCD/scripts" -c "
      source [find interface/kitprog3.cfg];
      transport select swd;
      source [find target/infineon/cy8c6xx.cfg];
      adapter srst pulse_width 200;
      adapter srst delay 500;
      init;
      reset run;
      exit" 2>&1)

  if echo "$OUT" | grep -q "Examination succeed"; then
    echo "✅ Reset OK on attempt $i — watch your serial monitor for boot logs."
    exit 0
  fi
  echo "… attempt $i/$TRIES failed (acquire) — keep the pins pressed, retrying"
done

echo "❌ Could not acquire after $TRIES attempts."
echo "   Press the JPROG1 pins in firmly (esp. XRES_N + GND + SWDIO/SWDCLK) and rerun."
exit 1

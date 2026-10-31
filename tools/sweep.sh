#!/bin/bash
# The regression sweep: all five levels plus the menu, checking for shader
# errors and for frames that have gone dark. Run it before pushing anything
# that touches rendering, the shader or world generation, then look at the
# images: several real bugs rendered valid frames that were wrong.
#
# Takes a few minutes headless. Do not run two sweeps at once: they share one
# Xvfb display and write the same filenames into shots/.
#
# Exits non-zero if any shot failed (tools/shot.sh greps the run log for shader
# errors), or if any frame's mean luma falls outside the band for its level.
# A failed shader compile does not crash: raylib falls back to its default
# shader, the frame goes black and the frame rate goes up. This script used to
# exit 0 on such a build; the luma check catches it.
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
FRAME=${BACKROOMS_SHOTFRAME:-80}
FAIL=0

# Mean luma bands, 0-255, measured on a working build at this fixed corridor
# spot. They are wide on purpose: they catch a black or blown-out frame, not a
# small lighting change. Tightening one means re-measuring all six when the
# lighting changes.
#
# Level 3 is meant to be nearly black: the Red Halls sit around 12/255 at this
# spot. (It was 16 before the fog took its brightness from the local light.)
band_lo() { case $1 in 0) echo 45;; 1) echo 20;; 2) echo 55;; 3) echo 5;; 4) echo 12;; menu) echo 20;; esac; }
band_hi() { case $1 in 0) echo 150;; 1) echo 110;; 2) echo 190;; 3) echo 32;; 4) echo 95;; menu) echo 150;; esac; }

shots=()
for LV in 0 1 2 3 4; do
  echo "--- level $LV ---"
  if "$ROOT/tools/shot.sh" "rg_lv$LV.png" BACKROOMS_SEED=1337 BACKROOMS_LEVEL=$LV \
      BACKROOMS_POS="15,15,0.8" BACKROOMS_SHOTFRAME=$FRAME; then
    shots+=("$LV:$ROOT/shots/rg_lv$LV.png")
  else
    echo "!! level $LV capture FAILED"; FAIL=1
  fi
done
echo "--- menu ---"
if "$ROOT/tools/shot.sh" rg_menu.png BACKROOMS_SEED=1337 BACKROOMS_MENU=1 BACKROOMS_SHOTFRAME=$FRAME; then
  shots+=("menu:$ROOT/shots/rg_menu.png")
else
  echo "!! menu capture FAILED"; FAIL=1
fi

echo "--- exposure ---"
for entry in "${shots[@]}"; do
  key=${entry%%:*}; png=${entry#*:}
  lum=$("$ROOT/tools/pixdiff.py" luma "$png" 2>/dev/null | awk '{print $1}')
  if [ -z "$lum" ]; then
    echo "!! $key: could not read mean luma (is Pillow installed? tools/sandbox-setup.sh)"; FAIL=1; continue
  fi
  lo=$(band_lo "$key"); hi=$(band_hi "$key")
  if awk -v l="$lum" -v a="$lo" -v b="$hi" 'BEGIN{exit !(l>=a && l<=b)}'; then
    printf '   %-4s mean luma %8s   (band %s-%s)\n' "$key" "$lum" "$lo" "$hi"
  else
    printf '!! %-4s mean luma %8s   OUT OF BAND %s-%s\n' "$key" "$lum" "$lo" "$hi"; FAIL=1
  fi
done

if [ "$FAIL" -ne 0 ]; then
  echo "=== SWEEP FAILED — a dark frame is usually a silently failed shader compile ==="
  exit 1
fi
echo "=== sweep done — now look at the images ==="

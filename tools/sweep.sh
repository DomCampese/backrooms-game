#!/bin/bash
# The regression sweep: all five levels plus the menu, checking for shader
# errors. Run this before pushing anything that touches rendering, the shader
# or world generation — then LOOK AT THE IMAGES. A clean exit proves nothing;
# several real bugs here rendered perfectly valid frames that were wrong.
#
# Takes a few minutes headless. Do not run two sweeps at once — every run
# shares one Xvfb display.
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
FRAME=${BACKROOMS_SHOTFRAME:-80}
for LV in 0 1 2 3 4; do
  echo "--- level $LV ---"
  "$ROOT/tools/shot.sh" "rg_lv$LV.png" BACKROOMS_SEED=1337 BACKROOMS_LEVEL=$LV \
      BACKROOMS_POS="15,15,0.8" BACKROOMS_SHOTFRAME=$FRAME
done
echo "--- menu ---"
"$ROOT/tools/shot.sh" rg_menu.png BACKROOMS_SEED=1337 BACKROOMS_MENU=1 BACKROOMS_SHOTFRAME=$FRAME
echo "=== sweep done — now look at the images ==="

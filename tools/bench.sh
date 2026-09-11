#!/bin/bash
# A/B the frame cost of two builds on the same level. The sandbox has other work
# on it, so a single run swings about 15% — take the best of N instead, and
# interleave the two binaries so a slow patch hits both equally.
# usage: tools/bench.sh <binA> <binB> <level> [runs]
set -uo pipefail
A=${1:?}; B=${2:?}; LV=${3:-0}; N=${4:-3}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
export DISPLAY=${DISPLAY:-:99}
pgrep -x Xvfb >/dev/null || { Xvfb "$DISPLAY" -screen 0 1440x850x24 >/dev/null 2>&1 & sleep 2; }
TMP=$(mktemp -d); cd "$TMP" || exit 1
best(){ local bin=$1 bt=99999 s e t
  for _ in $(seq 1 "$N"); do
    s=$(date +%s%N)
    env BACKROOMS_SEED=1337 BACKROOMS_LEVEL=$LV BACKROOMS_POS=15,15,0.8 \
        BACKROOMS_SHOTFRAME=80 BACKROOMS_SHOT=b.png "$bin" >/dev/null 2>&1
    e=$(date +%s%N); t=$(( (e-s)/1000000 ))
    [ "$t" -lt "$bt" ] && bt=$t
  done; echo "$bt"; }
for _ in $(seq 1 "$N"); do :; done
TA=$(best "$A"); TB=$(best "$B")
echo "level $LV  best-of-$N   A=$(basename "$A") ${TA}ms   B=$(basename "$B") ${TB}ms   B/A=$(awk "BEGIN{printf \"%.2f\", $TB/$TA}")"
rm -rf "$TMP"

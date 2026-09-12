#!/bin/bash
# Interleaved A/B benchmark, native macOS or Linux/Xvfb.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
if [[ $(uname -s) == Linux ]]; then
    export DISPLAY=${DISPLAY:-:99}
    if ! pgrep -x Xvfb >/dev/null; then
        Xvfb "$DISPLAY" -screen 0 1440x850x24 >/dev/null 2>&1 &
        sleep 2
    fi
fi
exec python3 "$ROOT/tools/bench.py" "$@"

#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_SRC_DIR="$ROOT_DIR/frontend"
WORKSPACE_BUILD_DIR="$WORKSPACE_SRC_DIR/build"
WORKSPACE_BINARY="$WORKSPACE_BUILD_DIR/frontend_harris_lk"
IMAGE_BINARY="/opt/slambench/frontend/build/frontend_harris_lk"
LOG_FILE="${1:-$ROOT_DIR/frontend.log}"
FRAME0="$ROOT_DIR/data/euroc/frame0.png"
FRAME1="$ROOT_DIR/data/euroc/frame1.png"

# Environment info
ARCH="$(uname -m)"
CPU_MODEL="$(awk -F: '/model name/ {print $2; exit}' /proc/cpuinfo | sed 's/^ //')"
CPU_CORES="$(nproc)"
CPU_CACHE="$(awk -F: '/cache size/ {print $2; exit}' /proc/cpuinfo | sed 's/^ //')"
SCHED_POLICY="$(chrt -p $$ 2>/dev/null | awk -F: '/scheduling policy/ {gsub(/^ +/,"",$2); print $2; exit}')"
SCHED_PRIORITY="$(chrt -p $$ 2>/dev/null | awk -F: '/scheduling priority/ {gsub(/^ +/,"",$2); print $2; exit}')"
CPU_AFFINITY="$(taskset -pc $$ 2>/dev/null | awk -F: '{gsub(/^ +/,"",$2); print $2; exit}')"
CPU_GOVERNOR="$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || true)"

print_section() {
  local title="$1"
  echo
  echo "========== $title =========="
  echo
}

if [[ ! -f "$FRAME0" || ! -f "$FRAME1" ]]; then
  echo "[frontend] dataset not found: $FRAME0 / $FRAME1"
  exit 1
fi

if [[ -f "$WORKSPACE_SRC_DIR/CMakeLists.txt" ]]; then
  mkdir -p "$WORKSPACE_BUILD_DIR"
  cmake -S "$WORKSPACE_SRC_DIR" -B "$WORKSPACE_BUILD_DIR" -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build "$WORKSPACE_BUILD_DIR" --config Release -j >/dev/null
  BINARY="$WORKSPACE_BINARY"
  MODE="workspace"
elif [[ -x "$IMAGE_BINARY" ]]; then
  BINARY="$IMAGE_BINARY"
  MODE="image"
else
  echo "[frontend] neither workspace source nor image binary found"
  echo "[frontend] expected one of:"
  echo "  - $WORKSPACE_SRC_DIR/CMakeLists.txt"
  echo "  - $IMAGE_BINARY"
  exit 1
fi

print_section "Environment"
echo "[frontend] arch           : $ARCH"
echo "[frontend] cpu model      : $CPU_MODEL"
echo "[frontend] cpu cores      : $CPU_CORES"
echo "[frontend] cpu cache      : $CPU_CACHE"
echo "[frontend] sched policy   : $SCHED_POLICY"
echo "[frontend] sched priority : $SCHED_PRIORITY"
echo "[frontend] cpu affinity   : $CPU_AFFINITY"
echo "[frontend] cpu governor   : $CPU_GOVERNOR"

CMD="$BINARY $FRAME0 $FRAME1"
echo "[frontend] mode: $MODE"
echo "[frontend] command: $CMD"
$CMD | tee "$LOG_FILE"

echo "[frontend] done. Log: $LOG_FILE"

# Generate summary table
python3 - "$LOG_FILE" <<'PYEOF'
import re
import sys
import os

def make_table(headers, rows):
    import prettytable
    t = prettytable.PrettyTable()
    t.field_names = headers
    for row in rows:
        t.add_row(row)
    t.align = "r"
    t.align[headers[0]] = "l"
    return str(t)


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

make_table() {
  local headers="$1"
  local rows="$2"
  python3 - <<PY
import sys

headers = "$headers".split('|')
rows_str = """$rows""".strip()

lines = []
if rows_str:
    for line in rows_str.split('\n'):
        if line.strip():
            lines.append([cell.strip() for cell in line.split('|')])

if not lines:
    print("No data")
    sys.exit(0)

col_widths = [len(h) for h in headers]
for row in lines:
    for i, cell in enumerate(row):
        col_widths[i] = max(col_widths[i], len(cell))

sep = "|" + "|".join("=" * (w + 1) for w in col_widths) + "|"
header = "|" + "|".join(f" {h:^{col_widths[i]}} " for i, h in enumerate(headers)) + "|"

lines_str = sep + "\n" + header + "\n" + sep + "\n"
for row in lines:
    lines_str += "|" + "|".join(f" {cell:^{col_widths[i]}} " for i, cell in enumerate(row)) + "|\n"
lines_str += sep

print(lines_str)
PY
}

summarize() {
  local log="$1"
  
  python3 - <<'PY'
import re
import sys
import os

def make_table(headers, rows):
    col_widths = [len(h) for h in headers]
    for row in rows:
        for i, cell in enumerate(row):
            if i < len(col_widths):
                col_widths[i] = max(col_widths[i], len(str(cell)))
    
    sep = "|" + "|".join("=" * (w + 1) for w in col_widths) + "|"
    header = "|" + "|".join(f" {h:^{col_widths[i]}} " for i, h in enumerate(headers)) + "|"
    
    lines_str = sep + "\n" + header + "\n" + sep + "\n"
    for row in rows:
        cells = [str(cell) for cell in row]
        lines_str += "|" + "|".join(f" {cell:^{col_widths[i]}} " for i, cell in enumerate(cells)) + "|\n"
    lines_str += sep
    return lines_str

log = sys.argv[1]

if not os.path.exists(log):
    print(f"[frontend] log not found: {log}")
    sys.exit(0)

with open(log, 'r') as f:
    content = f.read()

# Parse key metrics
image_size = re.search(r'image_size:\s*(\d+)x(\d+)', content)
detected = re.search(r'detected_points:\s*(\d+)', content)
tracked = re.search(r'tracked_points:\s*(\d+)', content)
load_ms = re.search(r'load_ms:\s*([\d.]+)', content)
gray_ms = re.search(r'gray_ms:\s*([\d.]+)', content)
harris_ms = re.search(r'harris_ms:\s*([\d.]+)', content)
lk_ms = re.search(r'lk_ms:\s*([\d.]+)', content)
total_ms = re.search(r'total_ms:\s*([\d.]+)', content)

rows = []
if all([image_size, detected, tracked, harris_ms, lk_ms, total_ms]):
    w, h = image_size.group(1), image_size.group(2)
    det = int(detected.group(1))
    trk = int(tracked.group(1))
    h_ms = float(harris_ms.group(1))
    l_ms = float(lk_ms.group(1))
    tot = float(total_ms.group(1))
    
    success_rate = (trk / det * 100) if det > 0 else 0
    rows.append(["image_size", f"{w}x{h}"])
    rows.append(["detected_points", f"{det}"])
    rows.append(["tracked_points", f"{trk}"])
    rows.append(["track_success_%", f"{success_rate:.1f}"])
    rows.append(["harris_ms", f"{h_ms:.3f}"])
    rows.append(["lk_ms", f"{l_ms:.3f}"])
    rows.append(["total_ms", f"{tot:.3f}"])
    rows.append(["fps", f"{1000.0/tot:.1f}"])
else:
    rows.append(["parse_error", "could not parse log"])

headers = ["metric", "value"]
print("\n========== Summary ==========\n")
print(make_table(headers, rows))
PY
  "$log"
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
echo "[frontend] cpu cache     : $CPU_CACHE"
echo "[frontend] sched policy   : $SCHED_POLICY"
echo "[frontend] sched priority : $SCHED_PRIORITY"
echo "[frontend] cpu affinity   : $CPU_AFFINITY"
echo "[frontend] cpu governor   : $CPU_GOVERNOR"

CMD="$BINARY $FRAME0 $FRAME1"
echo "[frontend] mode: $MODE"
echo "[frontend] command: $CMD"
$CMD | tee "$LOG_FILE"

echo "[frontend] done. Log: $LOG_FILE"

summarize "$LOG_FILE"

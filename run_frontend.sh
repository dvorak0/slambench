#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_SRC_DIR="$ROOT_DIR/frontend"
WORKSPACE_BUILD_DIR="$WORKSPACE_SRC_DIR/build"
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

summarize_frontend_log() {
  local log_file="$1"

  python3 - "$log_file" <<'PYEOF'
import os
import re
import sys
import prettytable

log_path = sys.argv[1]
if not os.path.exists(log_path):
    print(f"[frontend] log not found: {log_path}")
    sys.exit(0)

with open(log_path, 'r') as f:
    content = f.read()

def make_table(headers, rows):
    table = prettytable.PrettyTable()
    table.field_names = headers
    for row in rows:
        table.add_row(row)
    table.align = "r"
    table.align[headers[0]] = "l"
    return table.get_string()

image_size = re.search(r'image_size:\s*(\d+)x(\d+)', content)
total_ms = re.search(r'total_ms:\s*([\d.]+)', content)
rows = []

if image_size and total_ms:
    w, h = image_size.group(1), image_size.group(2)
    tot = float(total_ms.group(1))
    rows.append(["image_size", f"{w}x{h}"])

    detected = re.search(r'detected_points:\s*(\d+)', content)
    tracked = re.search(r'tracked_points:\s*(\d+)', content)
    harris_ms = re.search(r'harris_ms:\s*([\d.]+)', content)
    lk_ms = re.search(r'lk_ms:\s*([\d.]+)', content)
    halide_response_ms = re.search(r'halide_response_ms:\s*([\d.]+)', content)
    halide_post_ms = re.search(r'halide_post_ms:\s*([\d.]+)', content)
    if all([detected, tracked, harris_ms, lk_ms]):
        det = int(detected.group(1))
        trk = int(tracked.group(1))
        h_ms = float(harris_ms.group(1))
        l_ms = float(lk_ms.group(1))
        success_rate = (trk / det * 100) if det > 0 else 0
        rows.append(["detected_points", f"{det}"])
        rows.append(["tracked_points", f"{trk}"])
        rows.append(["track_success_%", f"{success_rate:.1f}"])
        rows.append(["harris_ms", f"{h_ms:.3f}"])
        rows.append(["lk_ms", f"{l_ms:.3f}"])
        rows.append(["total_ms", f"{tot:.3f}"])
        rows.append(["fps", f"{1000.0/tot:.1f}"])
    elif all([detected, tracked, halide_response_ms, halide_post_ms, lk_ms]):
        det = int(detected.group(1))
        trk = int(tracked.group(1))
        hr_ms = float(halide_response_ms.group(1))
        hp_ms = float(halide_post_ms.group(1))
        l_ms = float(lk_ms.group(1))
        success_rate = (trk / det * 100) if det > 0 else 0
        rows.append(["detected_points", f"{det}"])
        rows.append(["tracked_points", f"{trk}"])
        rows.append(["track_success_%", f"{success_rate:.1f}"])
        rows.append(["halide_response_ms", f"{hr_ms:.3f}"])
        rows.append(["halide_post_ms", f"{hp_ms:.3f}"])
        rows.append(["lk_ms", f"{l_ms:.3f}"])
        rows.append(["total_ms", f"{tot:.3f}"])
        rows.append(["fps", f"{1000.0/tot:.1f}"])
    else:
        keypoints0 = re.search(r'keypoints0:\s*(\d+)', content)
        keypoints1 = re.search(r'keypoints1:\s*(\d+)', content)
        descriptors0 = re.search(r'descriptors0:\s*(\d+)', content)
        descriptors1 = re.search(r'descriptors1:\s*(\d+)', content)
        matched_pairs = re.search(r'matched_pairs:\s*(\d+)', content)
        orb_ms = re.search(r'orb_ms:\s*([\d.]+)', content)
        match_ms = re.search(r'match_ms:\s*([\d.]+)', content)
        if all([keypoints0, keypoints1, descriptors0, descriptors1, matched_pairs, orb_ms, match_ms]):
            rows.append(["keypoints0", keypoints0.group(1)])
            rows.append(["keypoints1", keypoints1.group(1)])
            rows.append(["descriptors0", descriptors0.group(1)])
            rows.append(["descriptors1", descriptors1.group(1)])
            rows.append(["matched_pairs", matched_pairs.group(1)])
            rows.append(["orb_ms", f"{float(orb_ms.group(1)):.3f}"])
            rows.append(["match_ms", f"{float(match_ms.group(1)):.3f}"])
            rows.append(["total_ms", f"{tot:.3f}"])
            rows.append(["fps", f"{1000.0/tot:.1f}"])
        else:
            rows.append(["parse_error", "could not parse frontend mode"])
else:
    rows.append(["parse_error", "could not parse log"])

print("\n========== Summary ==========\n")
print(make_table(["metric", "value"], rows))
PYEOF
}

run_one_frontend() {
  local frontend_name="$1"
  local workspace_binary="$2"
  local image_binary="$3"
  local log_file="$4"

  local binary=""
  local source_mode=""

  if [[ -f "$WORKSPACE_SRC_DIR/CMakeLists.txt" ]]; then
    mkdir -p "$WORKSPACE_BUILD_DIR"
    cmake -S "$WORKSPACE_SRC_DIR" -B "$WORKSPACE_BUILD_DIR" -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$WORKSPACE_BUILD_DIR" --config Release -j >/dev/null
    binary="$workspace_binary"
    source_mode="workspace"
  elif [[ -x "$image_binary" ]]; then
    binary="$image_binary"
    source_mode="image"
  else
    echo "[frontend] neither workspace source nor image binary found for $frontend_name"
    echo "[frontend] expected one of:"
    echo "  - $WORKSPACE_SRC_DIR/CMakeLists.txt"
    echo "  - $image_binary"
    exit 1
  fi

  print_section "Frontend: $frontend_name"
  echo "[frontend] frontend: $frontend_name"
  echo "[frontend] mode: $source_mode"
  echo "[frontend] command: $binary $FRAME0 $FRAME1"
  "$binary" "$FRAME0" "$FRAME1" | tee "$log_file"
  echo "[frontend] done. Log: $log_file"
  summarize_frontend_log "$log_file"
}

if [[ ! -f "$FRAME0" || ! -f "$FRAME1" ]]; then
  echo "[frontend] dataset not found: $FRAME0 / $FRAME1"
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

run_one_frontend \
  "harris_lk" \
  "$WORKSPACE_BUILD_DIR/frontend_harris_lk" \
  "/opt/slambench/frontend/build/frontend_harris_lk" \
  "$ROOT_DIR/frontend_harris_lk.log"

run_one_frontend \
  "halide_harris_lk" \
  "$WORKSPACE_BUILD_DIR/frontend_halide_harris_lk" \
  "/opt/slambench/frontend/build/frontend_halide_harris_lk" \
  "$ROOT_DIR/frontend_halide_harris_lk.log"

run_one_frontend \
  "orb_bf" \
  "$WORKSPACE_BUILD_DIR/frontend_orb_bf" \
  "/opt/slambench/frontend/build/frontend_orb_bf" \
  "$ROOT_DIR/frontend_orb_bf.log"

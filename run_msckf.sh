#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATASET="${1:-problem-16-22106-pre.txt}"
DATASET_PATH="$ROOT_DIR/data/dubrovnik/$DATASET"
BUILD_DIR="$ROOT_DIR/msckf_c/build"
WORKSPACE_BINARY_RR="$BUILD_DIR/msckf_runner_rr"
WORKSPACE_BINARY_RC="$BUILD_DIR/msckf_runner_rc"
WORKSPACE_BINARY_CR="$BUILD_DIR/msckf_runner_cr"
WORKSPACE_BINARY_CC="$BUILD_DIR/msckf_runner_cc"
WORKSPACE_BINARY_OPENBLAS="$BUILD_DIR/msckf_runner_openblas"
IMAGE_BINARY="/opt/slambench/msckf_c/build/msckf_runner"
IMAGE_BINARY_RR="/opt/slambench/msckf_c/build/msckf_runner_rr"
IMAGE_BINARY_RC="/opt/slambench/msckf_c/build/msckf_runner_rc"
IMAGE_BINARY_CR="/opt/slambench/msckf_c/build/msckf_runner_cr"
IMAGE_BINARY_CC="/opt/slambench/msckf_c/build/msckf_runner_cc"
IMAGE_BINARY_OPENBLAS="/opt/slambench/msckf_c/build/msckf_runner_openblas"
LOG_FILE="${2:-$ROOT_DIR/msckf.log}"
LOG_BASE="${LOG_FILE%.log}"
if [[ "$LOG_BASE" == "$LOG_FILE" ]]; then
  LOG_BASE="$LOG_FILE"
fi
LOG_RR="${LOG_BASE}_rr.log"
LOG_RC="${LOG_BASE}_rc.log"
LOG_CR="${LOG_BASE}_cr.log"
LOG_CC="${LOG_BASE}_cc.log"
LOG_OPENBLAS="${LOG_BASE}_openblas.log"

echo "[msckf] root: $ROOT_DIR"

if [[ ! -f "$DATASET_PATH" ]]; then
  echo "[msckf] dataset not found at $DATASET_PATH"
  exit 1
fi

if [[ -d "$ROOT_DIR/msckf_c" ]]; then
  cmake -S "$ROOT_DIR/msckf_c" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build "$BUILD_DIR" --config Release -j >/dev/null
  MODE="workspace"
elif [[ -x "$IMAGE_BINARY_RR" && -x "$IMAGE_BINARY_RC" && -x "$IMAGE_BINARY_CR" && -x "$IMAGE_BINARY_CC" ]]; then
  MODE="image_multi"
elif [[ -x "$IMAGE_BINARY" ]]; then
  MODE="image_single"
else
  echo "[msckf] neither workspace source nor image binary found"
  echo "[msckf] expected one of:"
  echo "  - $ROOT_DIR/msckf_c"
  echo "  - $IMAGE_BINARY"
  exit 1
fi

parse_total_time() {
  local log="$1"
  awk '/^TIME[[:space:]]/ {print $2}' "$log" | head -n1
}

parse_global_solve() {
  local log="$1"
  awk -F': ' '/^Global camera solve:/ {split($2,a," "); print a[1]; exit}' "$log"
}

print_result() {
  local name="$1"
  local log="$2"
  local t g
  t=$(parse_total_time "$log")
  g=$(parse_global_solve "$log")
  printf "%-26s %12s %12s\n" "$name" "${t:-n/a}" "${g:-n/a}"
}

run_comparison() {
  local bin_rr="$1"
  local bin_rc="$2"
  local bin_cr="$3"
  local bin_cc="$4"
  local bin_openblas="$5"

  echo "[msckf] running rr: row-major storage + row-order elimination..."
  "$bin_rr" "$DATASET_PATH" > "$LOG_RR" 2>&1

  echo "[msckf] running rc: row-major storage + col-order elimination..."
  "$bin_rc" "$DATASET_PATH" > "$LOG_RC" 2>&1

  echo "[msckf] running cr: col-major storage + row-order elimination..."
  "$bin_cr" "$DATASET_PATH" > "$LOG_CR" 2>&1

  echo "[msckf] running cc: col-major storage + col-order elimination..."
  "$bin_cc" "$DATASET_PATH" > "$LOG_CC" 2>&1

  if [[ -x "$bin_openblas" ]]; then
    echo "[msckf] running openblas: col-major storage + OpenBLAS dgels..."
    echo "[msckf] openblas threads: OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 GOTO_NUM_THREADS=1"
    OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 GOTO_NUM_THREADS=1 \
      "$bin_openblas" "$DATASET_PATH" > "$LOG_OPENBLAS" 2>&1
  fi

  cp "$LOG_RR" "$LOG_FILE"

  echo
  echo "[msckf] layout/order/openblas comparison (seconds):"
  printf "%-26s %12s %12s\n" "mode" "TIME(s)" "GlobalQR(s)"
  printf "%-26s %12s %12s\n" "--------------------------" "------------" "------------"
  print_result "rr (row-major,row-order)" "$LOG_RR"
  print_result "rc (row-major,col-order)" "$LOG_RC"
  print_result "cr (col-major,row-order)" "$LOG_CR"
  print_result "cc (col-major,col-order)" "$LOG_CC"
  if [[ -f "$LOG_OPENBLAS" ]]; then
    print_result "openblas (col-major,dgels)" "$LOG_OPENBLAS"
  fi

  echo
  echo "[msckf] logs:"
  echo "  rr -> $LOG_RR"
  echo "  rc -> $LOG_RC"
  echo "  cr -> $LOG_CR"
  echo "  cc -> $LOG_CC"
  if [[ -f "$LOG_OPENBLAS" ]]; then
    echo "  openblas -> $LOG_OPENBLAS"
  else
    echo "  openblas -> not built (OpenBLAS not found by CMake)"
  fi
  echo "  default -> $LOG_FILE (rr, for compatibility)"
}

if [[ "$MODE" == "workspace" ]]; then
  run_comparison "$WORKSPACE_BINARY_RR" "$WORKSPACE_BINARY_RC" "$WORKSPACE_BINARY_CR" "$WORKSPACE_BINARY_CC" "$WORKSPACE_BINARY_OPENBLAS"
elif [[ "$MODE" == "image_multi" ]]; then
  run_comparison "$IMAGE_BINARY_RR" "$IMAGE_BINARY_RC" "$IMAGE_BINARY_CR" "$IMAGE_BINARY_CC" "$IMAGE_BINARY_OPENBLAS"
else
  CMD="$IMAGE_BINARY $DATASET_PATH"
  echo "[msckf] command: $CMD"
  "$IMAGE_BINARY" "$DATASET_PATH" > "$LOG_FILE" 2>&1
  echo "[msckf] done. Log: $LOG_FILE"
  echo "[msckf] note: image mode only has one runner, comparison unavailable."
fi

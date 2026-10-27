#!/usr/bin/env bash
# Run upstream GTSAM's timing/timeSFMBAL on a BAL dataset.
#
# This exercises the point-batched multifrontal solver path recommended in
# https://github.com/borglab/gtsam/issues/2299 (PR #2694), which the GTSAM fork
# pinned in this repo predates. Requires the `slambench:gtsam-upstream` image
# (.devcontainer/Dockerfile.gtsam-upstream).
#
# Usage: run_gtsam_upstream.sh [dataset_name] [log_file] [extra timeSFMBAL flags]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATASET="${1:-problem-16-22106-pre.txt}"
DATASET_PATH="$ROOT_DIR/data/dubrovnik/$DATASET"
LOG_FILE="${2:-$ROOT_DIR/gtsam_upstream.log}"
EXTRA_FLAGS="${3:-}"

BINARY="${TIMESFMBAL_BIN:-/opt/slambench/gtsam-upstream/build/timing/timeSFMBAL}"

if [[ ! -f "$DATASET_PATH" ]]; then
  echo "[gtsam-upstream] dataset not found at $DATASET_PATH"
  exit 1
fi

if [[ ! -x "$BINARY" ]]; then
  echo "[gtsam-upstream] binary not found at $BINARY"
  echo "Build and use the slambench:gtsam-upstream image first."
  exit 1
fi

CMD="$BINARY $EXTRA_FLAGS $DATASET_PATH"
echo "[gtsam-upstream] command: $CMD"
TIMEFORMAT="TIME %R"
{ time eval "$CMD"; } > "$LOG_FILE" 2>&1

echo "[gtsam-upstream] done. Log: $LOG_FILE"
awk '/^\| Dataset/ , 0' "$LOG_FILE" | head -5
echo "[gtsam-upstream] wall: $(awk '/^TIME[[:space:]]/ {print $2}' "$LOG_FILE" | head -n1) s"

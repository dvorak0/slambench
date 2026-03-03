#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATASET="problem-16-22106-pre.txt"
DATASET_PATH="$ROOT_DIR/data/dubrovnik/$DATASET"
BINARY="/opt/slambench/gtsam/build/examples/SFMExample_bal"
LOG_FILE="$ROOT_DIR/gtsam.log"

echo "[gtsam] root: $ROOT_DIR"

if [[ ! -f "$DATASET_PATH" ]]; then
  echo "[gtsam] dataset not found at $DATASET_PATH"
  exit 1
fi

if [[ ! -x "$BINARY" ]]; then
  echo "[gtsam] binary not found at $BINARY"
  echo "Ensure the devcontainer image was built (it builds gtsam examples)."
  exit 1
fi

CMD="$BINARY $DATASET_PATH"
echo "[gtsam] command: $CMD"
TIMEFORMAT="TIME %R"
{ time eval "$CMD"; } > "$LOG_FILE" 2>&1

echo "[gtsam] done. Log: $LOG_FILE"

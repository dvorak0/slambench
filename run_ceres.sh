#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATASET="problem-16-22106-pre.txt"
DATASET_PATH="$ROOT_DIR/data/dubrovnik/$DATASET"
BINARY="/opt/ceres-solver/build/bin/bundle_adjuster"
LOG_FILE="$ROOT_DIR/bundle_adjustment_dubrovnik_ceres.log"

echo "[ceres] root: $ROOT_DIR"

if [[ ! -f "$DATASET_PATH" ]]; then
  echo "[ceres] dataset not found at $DATASET_PATH"
  echo "Please add the dataset file (e.g., data/dubrovnik/$DATASET) to the repo."
  exit 1
fi

if [[ ! -x "$BINARY" ]]; then
  echo "[ceres] binary not found at $BINARY"
  echo "Ensure the devcontainer image was built (it builds ceres with examples)."
  exit 1
fi

CMD="$BINARY --input $DATASET_PATH -num_threads=1 -linear_solver=dense_schur"
echo "[ceres] command: $CMD"
eval "$CMD" | tee "$LOG_FILE"

echo "[ceres] done. Log: $LOG_FILE"

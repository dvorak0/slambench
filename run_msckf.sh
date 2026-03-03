#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATASET="problem-16-22106-pre.txt"
DATASET_PATH="$ROOT_DIR/data/dubrovnik/$DATASET"
BUILD_DIR="$ROOT_DIR/msckf_c/build"
BINARY="$BUILD_DIR/msckf_runner"
LOG_FILE="$ROOT_DIR/msckf.log"

echo "[msckf] root: $ROOT_DIR"

if [[ ! -f "$DATASET_PATH" ]]; then
  echo "[msckf] dataset not found at $DATASET_PATH"
  exit 1
fi

cmake -S "$ROOT_DIR/msckf_c" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD_DIR" --config Release -j >/dev/null

CMD="$BINARY $DATASET_PATH"
echo "[msckf] command: $CMD"
eval "$CMD" > "$LOG_FILE" 2>&1

echo "[msckf] done. Log: $LOG_FILE"

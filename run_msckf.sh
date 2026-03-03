#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATASET="problem-16-22106-pre.txt"
DATASET_PATH="$ROOT_DIR/data/dubrovnik/$DATASET"
BINARY="/opt/slambench/msckf_c/build/msckf_runner"
LOG_FILE="$ROOT_DIR/msckf.log"

echo "[msckf] root: $ROOT_DIR"

if [[ ! -f "$DATASET_PATH" ]]; then
  echo "[msckf] dataset not found at $DATASET_PATH"
  exit 1
fi

if [[ ! -x "$BINARY" ]]; then
  echo "[msckf] binary not found at $BINARY"
  exit 1
fi

CMD="$BINARY $DATASET_PATH"
echo "[msckf] command: $CMD"
eval "$CMD" > "$LOG_FILE" 2>&1

echo "[msckf] done. Log: $LOG_FILE"

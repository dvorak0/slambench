#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATASET="problem-16-22106-pre.txt"
DATASET_PATH="$ROOT_DIR/data/dubrovnik/$DATASET"
BUILD_DIR="$ROOT_DIR/msckf_c/build"
WORKSPACE_BINARY="$BUILD_DIR/msckf_runner"
IMAGE_BINARY="/opt/slambench/msckf_c/build/msckf_runner"
LOG_FILE="$ROOT_DIR/msckf.log"

echo "[msckf] root: $ROOT_DIR"

if [[ ! -f "$DATASET_PATH" ]]; then
  echo "[msckf] dataset not found at $DATASET_PATH"
  exit 1
fi

if [[ -d "$ROOT_DIR/msckf_c" ]]; then
  cmake -S "$ROOT_DIR/msckf_c" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build "$BUILD_DIR" --config Release -j >/dev/null
  BINARY="$WORKSPACE_BINARY"
elif [[ -x "$IMAGE_BINARY" ]]; then
  BINARY="$IMAGE_BINARY"
else
  echo "[msckf] neither workspace source nor image binary found"
  echo "[msckf] expected one of:"
  echo "  - $ROOT_DIR/msckf_c"
  echo "  - $IMAGE_BINARY"
  exit 1
fi

CMD="$BINARY $DATASET_PATH"
echo "[msckf] command: $CMD"
eval "$CMD" > "$LOG_FILE" 2>&1

echo "[msckf] done. Log: $LOG_FILE"

#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/frontend/build"
LOG_FILE="$ROOT_DIR/frontend.log"
FRAME0="$ROOT_DIR/data/euroc/frame0.png"
FRAME1="$ROOT_DIR/data/euroc/frame1.png"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake ..
make -j"$(nproc)"

CMD="$BUILD_DIR/frontend_harris_lk $FRAME0 $FRAME1"
echo "[frontend] command: $CMD"
$CMD | tee "$LOG_FILE"

echo "[frontend] done. Log: $LOG_FILE"

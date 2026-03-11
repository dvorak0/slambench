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

CMD="$BINARY $FRAME0 $FRAME1"
echo "[frontend] mode: $MODE"
echo "[frontend] command: $CMD"
$CMD | tee "$LOG_FILE"

echo "[frontend] done. Log: $LOG_FILE"

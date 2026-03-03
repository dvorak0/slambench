#!/usr/bin/env bash
set -euo pipefail

# Workspace root (assumes script is run from repo root inside the devcontainer)
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Symforce is installed and built in the image at /opt/slambench/symforce
SYMFORCE_DIR="/opt/slambench/symforce"
BUILD_DIR="$SYMFORCE_DIR/build"
DATASET="problem-16-22106-pre.txt"
# Expect dataset to be checked into the repo under data/dubrovnik/
DATASET_PATH="$ROOT_DIR/data/dubrovnik/$DATASET"
LOG_FILE="$ROOT_DIR/bundle_adjustment_dubrovnik.log"

echo "[symforce] root: $ROOT_DIR"
echo "[symforce] dataset: $DATASET_PATH"

# Run and log (binary already built in image)
echo "[symforce] running example..."
CMD="$BUILD_DIR/bin/examples/bundle_adjustment_in_the_large_example $DATASET_PATH"
echo "[symforce] command: $CMD"
eval "$CMD" | tee "$LOG_FILE"

echo "[symforce] done. Log: $LOG_FILE"

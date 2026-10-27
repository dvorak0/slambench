#!/usr/bin/env bash
# Run g2o's BAL example (examples/bal/bal_example) on a BAL dataset.
#
# Usage: run_g2o.sh [iterations] [log_file] [dataset_name]
#
# Solver selection (g2o picks CHOLMOD at build time when SuiteSparse is found,
# Eigen sparse otherwise):
#   G2O_FLAGS="-pcg"   -> use the PCG (conjugate gradient) linear solver
#
# NOTE: bal_example applies no robust kernel, so its chi2 is directly
# comparable to Ceres' cost / GTSAM's error on the same BAL file.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATASET="${3:-problem-16-22106-pre.txt}"
DATASET_PATH="$ROOT_DIR/data/dubrovnik/$DATASET"
BINARY="${G2O_BAL_BINARY:-/opt/slambench/g2o/build/bin/bal_example}"
ITERATIONS="${1:-5}"
LOG_FILE="${2:-$ROOT_DIR/g2o.log}"
SOLVER_FLAGS="${G2O_FLAGS:-}"

echo "[g2o] root: $ROOT_DIR"

if [[ ! -f "$DATASET_PATH" ]]; then
  echo "[g2o] dataset not found at $DATASET_PATH"
  exit 1
fi

if [[ ! -x "$BINARY" ]]; then
  echo "[g2o] binary not found at $BINARY"
  echo "Ensure the devcontainer image was built (it builds g2o's bal_example)."
  exit 1
fi

CMD="$BINARY -i $ITERATIONS -v $SOLVER_FLAGS $DATASET_PATH"
echo "[g2o] command: $CMD"
TIMEFORMAT="TIME %R"
{ time eval "$CMD"; } > "$LOG_FILE" 2>&1

echo "[g2o] done. Log: $LOG_FILE"
echo "[g2o] wall: $(awk '/^TIME[[:space:]]/ {print $2}' "$LOG_FILE" | head -n1) s"

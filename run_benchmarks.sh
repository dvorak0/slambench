#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SYM_LOG="$ROOT_DIR/bundle_adjustment_dubrovnik.log"
CERES_LOG="$ROOT_DIR/bundle_adjustment_dubrovnik_ceres.log"

echo "[bench] running symforce..."
bash "$ROOT_DIR/run_symforce.sh"

echo "[bench] running ceres..."
bash "$ROOT_DIR/run_ceres.sh"

parse_symforce_total() {
  local log="$1"
  awk -F'|' '/Optimizer<sym::Optimize>::Optimize/ {gsub(/ /,"",$3); print $3}' "$log" | head -n1
}

parse_ceres_total() {
  local log="$1"
  awk '/^Total[[:space:]]/ {print $2}' "$log" | head -n1
}

SYM_T=$(parse_symforce_total "$SYM_LOG")
CERES_T=$(parse_ceres_total "$CERES_LOG")

if [[ -z "$SYM_T" || -z "$CERES_T" ]]; then
  echo "[bench] failed to parse totals (symforce: '$SYM_T', ceres: '$CERES_T')"
  exit 1
fi

echo "[bench] summary:"
echo "  symforce total time (s): $SYM_T"
echo "  ceres total time (s):    $CERES_T"

# Compute ratio using python for reliability
python3 - <<PY
sym = float("$SYM_T")
ceres = float("$CERES_T")
ratio = sym / ceres if ceres > 0 else float('inf')
print(f"  ratio symforce/ceres:    {ratio:.3f}x")
PY

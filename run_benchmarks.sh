#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SYM_LOG="$ROOT_DIR/symforce.log"
CERES_LOG="$ROOT_DIR/ceres.log"
ARCH="$(uname -m)"

echo "[bench] running symforce..."
bash "$ROOT_DIR/run_symforce.sh"

echo "[bench] running ceres..."
bash "$ROOT_DIR/run_ceres.sh"

parse_symforce_total() {
  local log="$1"
  awk -F'|' '/Optimizer<sym::Optimize>::Optimize/ {gsub(/ /,"",$3); print $3}' "$log" | head -n1
}

parse_symforce_iters() {
  local log="$1"
  # Prefer explicit "Finished in X iterations"
  local finished
  finished=$(awk '/Finished in [0-9]+ iterations/ {for(i=1;i<=NF;i++){if($i ~ /^[0-9]+$/){print $i; exit}}}' "$log")
  if [[ -n "${finished:-}" ]]; then
    echo "$finished"
    return
  fi
  # Fallback: count iteration lines
  local count
  count=$(grep -c 'LM<sym::Optimize> \[iter' "$log" || true)
  echo "${count:-0}"
}

parse_ceres_total() {
  local log="$1"
  awk '/^Total[[:space:]]/ {print $2}' "$log" | head -n1
}

parse_ceres_iters() {
  local log="$1"
  # Lines starting with an iteration number in the table
  awk '/^[[:space:]]*[0-9]+[[:space:]]/ {print}' "$log" | wc -l
}

SYM_T=$(parse_symforce_total "$SYM_LOG")
CERES_T=$(parse_ceres_total "$CERES_LOG")
SYM_ITERS=$(parse_symforce_iters "$SYM_LOG")
CERES_ITERS=$(parse_ceres_iters "$CERES_LOG")

if [[ -z "$SYM_T" || -z "$CERES_T" || -z "$SYM_ITERS" || -z "$CERES_ITERS" || "$SYM_ITERS" -eq 0 || "$CERES_ITERS" -eq 0 ]]; then
  echo "[bench] failed to parse totals/iters (symforce: '$SYM_T' iters=$SYM_ITERS, ceres: '$CERES_T' iters=$CERES_ITERS)"
  exit 1
fi

python3 - <<PY
sym_total = float("$SYM_T")
ceres_total = float("$CERES_T")
sym_iters = int("$SYM_ITERS")
ceres_iters = int("$CERES_ITERS")

sym_per_iter = sym_total / sym_iters if sym_iters > 0 else float('nan')
ceres_per_iter = ceres_total / ceres_iters if ceres_iters > 0 else float('nan')
ratio_total = sym_total / ceres_total if ceres_total > 0 else float('inf')
ratio_iter = sym_per_iter / ceres_per_iter if ceres_per_iter > 0 else float('inf')

print(f"[bench] arch: {os.environ.get('ARCH', '?')}")
print("[bench] summary (seconds):")
print(f"{'engine':10} {'total':>8} {'iters':>7} {'per_iter':>10}")
print(f"{'symforce':10} {sym_total:8.3f} {sym_iters:7d} {sym_per_iter:10.3f}")
print(f"{'ceres':10} {ceres_total:8.3f} {ceres_iters:7d} {ceres_per_iter:10.3f}")
print(f"{'ratio sym/ceres':>24} {ratio_total:8.3f} {'' :7} {ratio_iter:10.3f}x")
PY

#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SYM_LOG="$ROOT_DIR/symforce.log"
CERES_LOG="$ROOT_DIR/ceres.log"
CERES_SPARSE_LOG="$ROOT_DIR/ceres_sparse.log"
GTSAM_LOG="$ROOT_DIR/gtsam.log"
MSCKF_LOG="$ROOT_DIR/msckf.log"
ARCH="$(uname -m)"
CPU_MODEL="$(awk -F: '/model name/ {print $2; exit}' /proc/cpuinfo | sed 's/^ //')"
CPU_CORES="$(nproc)"
CPU_CACHE="$(awk -F: '/cache size/ {print $2; exit}' /proc/cpuinfo | sed 's/^ //')"

echo "[bench] running symforce..."
bash "$ROOT_DIR/run_symforce.sh"

echo "[bench] running ceres..."
bash "$ROOT_DIR/run_ceres.sh" dense_schur "$CERES_LOG"

echo "[bench] running ceres (sparse_schur)..."
bash "$ROOT_DIR/run_ceres.sh" sparse_schur "$CERES_SPARSE_LOG"

echo "[bench] running gtsam..."
bash "$ROOT_DIR/run_gtsam.sh"

echo "[bench] running msckf..."
bash "$ROOT_DIR/run_msckf.sh"

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

parse_gtsam_total() {
  local log="$1"
  awk '/^TIME[[:space:]]/ {print $2}' "$log" | head -n1
}

parse_gtsam_iters() {
  local log="$1"
  # SFMExample_bal does not print iterations; if we ever log them, pick them up
  local iter
  iter=$(awk '/iterations:/ {for(i=1;i<=NF;i++){if($i ~ /^[0-9]+$/){print $i; exit}}}' "$log")
  if [[ -n "${iter:-}" ]]; then
    echo "$iter"
    return
  fi
  # Fallback: treat as a single iteration
  echo 1
}

parse_msckf_total() {
  local log="$1"
  awk '/^TIME[[:space:]]/ {print $2}' "$log" | head -n1
}

parse_msckf_iters() {
  local log="$1"
  awk '/^iterations:/ {print $2}' "$log" | head -n1
}

SYM_T=$(parse_symforce_total "$SYM_LOG")
CERES_T=$(parse_ceres_total "$CERES_LOG")
CERES_SPARSE_T=$(parse_ceres_total "$CERES_SPARSE_LOG")
GTSAM_T=$(parse_gtsam_total "$GTSAM_LOG")
MSCKF_T=$(parse_msckf_total "$MSCKF_LOG")
SYM_ITERS=$(parse_symforce_iters "$SYM_LOG")
CERES_ITERS=$(parse_ceres_iters "$CERES_LOG")
CERES_SPARSE_ITERS=$(parse_ceres_iters "$CERES_SPARSE_LOG")
GTSAM_ITERS=$(parse_gtsam_iters "$GTSAM_LOG")
MSCKF_ITERS=$(parse_msckf_iters "$MSCKF_LOG")

if [[ -z "$SYM_T" || -z "$CERES_T" || -z "$CERES_SPARSE_T" || -z "$GTSAM_T" || -z "$MSCKF_T" || -z "$SYM_ITERS" || -z "$CERES_ITERS" || -z "$CERES_SPARSE_ITERS" || -z "$GTSAM_ITERS" || -z "$MSCKF_ITERS" || "$SYM_ITERS" -eq 0 || "$CERES_ITERS" -eq 0 || "$CERES_SPARSE_ITERS" -eq 0 || "$GTSAM_ITERS" -eq 0 || "$MSCKF_ITERS" -eq 0 ]]; then
  echo "[bench] failed to parse totals/iters (symforce: '$SYM_T' iters=$SYM_ITERS, ceres_dense: '$CERES_T' iters=$CERES_ITERS, ceres_sparse: '$CERES_SPARSE_T' iters=$CERES_SPARSE_ITERS, gtsam: '$GTSAM_T' iters=$GTSAM_ITERS, msckf: '$MSCKF_T' iters=$MSCKF_ITERS)"
  exit 1
fi

export ARCH CPU_MODEL CPU_CORES CPU_CACHE

python3 - <<PY
import os
sym_total = float("$SYM_T")
ceres_total = float("$CERES_T")
ceres_sparse_total = float("$CERES_SPARSE_T")
gtsam_total = float("$GTSAM_T")
msckf_total = float("$MSCKF_T")
sym_iters = int("$SYM_ITERS")
ceres_iters = int("$CERES_ITERS")
ceres_sparse_iters = int("$CERES_SPARSE_ITERS")
gtsam_iters = int("$GTSAM_ITERS")
msckf_iters = int("$MSCKF_ITERS")

sym_per_iter = sym_total / sym_iters if sym_iters > 0 else float('nan')
ceres_per_iter = ceres_total / ceres_iters if ceres_iters > 0 else float('nan')
ceres_sparse_per_iter = ceres_sparse_total / ceres_sparse_iters if ceres_sparse_iters > 0 else float('nan')
gtsam_per_iter = gtsam_total / gtsam_iters if gtsam_iters > 0 else float('nan')
msckf_per_iter = msckf_total / msckf_iters if msckf_iters > 0 else float('nan')
sym_rate = sym_iters / sym_total if sym_total > 0 else float('nan')
ceres_rate = ceres_iters / ceres_total if ceres_total > 0 else float('nan')
ceres_sparse_rate = ceres_sparse_iters / ceres_sparse_total if ceres_sparse_total > 0 else float('nan')
gtsam_rate = gtsam_iters / gtsam_total if gtsam_total > 0 else float('nan')
msckf_rate = msckf_iters / msckf_total if msckf_total > 0 else float('nan')

arch = os.environ.get("ARCH", "?")
cpu_model = os.environ.get("CPU_MODEL", "?")
cpu_cores = os.environ.get("CPU_CORES", "?")
cpu_cache = os.environ.get("CPU_CACHE", "?")

def make_table(headers, rows):
    widths = [
        max(len(str(headers[i])), *(len(str(r[i])) for r in rows))
        for i in range(len(headers))
    ]
    align = ["<"] + [">"] * (len(headers) - 1)

    def sep(char="-"):
        return "+" + "+".join(char * (w + 2) for w in widths) + "+"

    def fmt_row(row):
        return "|" + "|".join(
            f" {str(row[i]):{align[i]}{widths[i]}} " for i in range(len(headers))
        ) + "|"

    return "\n".join(
        [sep("-"), fmt_row(headers), sep("=")]
        + [fmt_row(r) for r in rows]
        + [sep("-")]
    )

headers = ["engine", "total (s)", "iters", "per_iter (s)", "iter/s", "per_iter ratio"]

results = [
    ("symforce", sym_total, sym_iters, sym_per_iter, sym_rate),
    ("ceres_dense", ceres_total, ceres_iters, ceres_per_iter, ceres_rate),
    ("ceres_sparse", ceres_sparse_total, ceres_sparse_iters, ceres_sparse_per_iter, ceres_sparse_rate),
    ("gtsam", gtsam_total, gtsam_iters, gtsam_per_iter, gtsam_rate),
    ("msckf", msckf_total, msckf_iters, msckf_per_iter, msckf_rate),
]

base_per_iter = ceres_per_iter if ceres_per_iter > 0 else None

rows = []
for name, total, iters, per_iter, rate in results:
    ratio = per_iter / base_per_iter if base_per_iter and base_per_iter > 0 else float("inf")
    rows.append(
        [
            name,
            f"{total:.3f}",
            f"{iters:d}",
            f"{per_iter:.3f}",
            f"{rate:.2f}",
            f"{ratio:.3f}x" if base_per_iter and base_per_iter > 0 else "n/a",
        ]
    )

print(f"[bench] cpu: {cpu_model} | cores: {cpu_cores} | cache: {cpu_cache} | arch: {arch}")
print("[bench] summary:")
print(make_table(headers, rows))
PY

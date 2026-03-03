#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SYM_LOG="$ROOT_DIR/symforce.log"
CERES_LOG="$ROOT_DIR/ceres.log"
ARCH="$(uname -m)"
CPU_MODEL="$(awk -F: '/model name/ {print $2; exit}' /proc/cpuinfo | sed 's/^ //')"
CPU_CORES="$(nproc)"
CPU_CACHE="$(awk -F: '/cache size/ {print $2; exit}' /proc/cpuinfo | sed 's/^ //')"

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

export ARCH CPU_MODEL CPU_CORES CPU_CACHE

python3 - <<PY
import os
sym_total = float("$SYM_T")
ceres_total = float("$CERES_T")
sym_iters = int("$SYM_ITERS")
ceres_iters = int("$CERES_ITERS")

sym_per_iter = sym_total / sym_iters if sym_iters > 0 else float('nan')
ceres_per_iter = ceres_total / ceres_iters if ceres_iters > 0 else float('nan')
sym_rate = sym_iters / sym_total if sym_total > 0 else float('nan')
ceres_rate = ceres_iters / ceres_total if ceres_total > 0 else float('nan')
ratio_total = sym_total / ceres_total if ceres_total > 0 else float('inf')
ratio_iter = sym_per_iter / ceres_per_iter if ceres_per_iter > 0 else float('inf')
ratio_rate = sym_rate / ceres_rate if ceres_rate > 0 else float('inf')

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

rows = [
    [
        "symforce",
        f"{sym_total:.3f}",
        f"{sym_iters:d}",
        f"{sym_per_iter:.3f}",
        f"{sym_rate:.2f}",
        f"{ratio_iter:.3f}x",
    ],
    [
        "ceres",
        f"{ceres_total:.3f}",
        f"{ceres_iters:d}",
        f"{ceres_per_iter:.3f}",
        f"{ceres_rate:.2f}",
        "1.000x",
    ],
]

print(f"[bench] cpu: {cpu_model} | cores: {cpu_cores} | cache: {cpu_cache} | arch: {arch}")
print("[bench] summary:")
print(make_table(headers, rows))
PY

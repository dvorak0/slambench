#!/usr/bin/env bash
# Ceres vs GTSAM vs g2o head-to-head on the same BAL problem.
#
# Reproduces the comparison from https://github.com/borglab/gtsam/issues/2299
# (CERES vs GTSAM on BAL-16) and adds g2o as a third engine.
#
# Usage: run_bal_three_way.sh [dataset_name]
#
# All engines run with no robust kernel on the same input file, so the final
# cost/chi2 columns are directly comparable. Wall time is measured around the
# whole process (parse + optimize + write), i.e. what `time ./binary` reports.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATASET="${1:-problem-16-22106-pre.txt}"
DATASET_PATH="$ROOT_DIR/data/dubrovnik/$DATASET"

CERES_BIN="${CERES_BIN:-/opt/slambench/ceres-solver/build/bin/bundle_adjuster}"
GTSAM_BAL_BIN="${GTSAM_BAL_BIN:-/opt/slambench/gtsam/build/examples/SFMExample_bal}"
GTSAM_SMART_BIN="${GTSAM_SMART_BIN:-/opt/slambench/gtsam/build/examples/SFMExample_SmartFactor_bal}"
G2O_BAL_BIN="${G2O_BAL_BIN:-/opt/slambench/g2o/build/bin/bal_example}"
SYMFORCE_BIN="${SYMFORCE_BIN:-/opt/slambench/symforce/build/bin/examples/bundle_adjustment_in_the_large_example}"

# Ceres' bundle_adjuster and g2o's bal_example both default to 5 iterations.
# GTSAM's examples run to their own convergence test, so iteration counts differ
# by design -- report them and also compare per-iteration time.
ITERS="${THREE_WAY_ITERS:-5}"
REPEAT="${THREE_WAY_REPEAT:-1}"

OUT_DIR="${THREE_WAY_OUT:-$ROOT_DIR/three_way_out}"
mkdir -p "$OUT_DIR"

if [[ ! -f "$DATASET_PATH" ]]; then
  echo "[3way] dataset not found at $DATASET_PATH"
  exit 1
fi

tag="$(echo "$DATASET" | sed 's/\.txt$//; s/=/ /g; s/ /_/g')"

# run_timed <logfile> <command...>  -> echoes wall seconds
run_timed() {
  local log="$1"; shift
  local t0 t1
  t0=$(date +%s%N)
  if ! "$@" > "$log" 2>&1; then
    echo "[3way] FAILED: $*" >&2
    return 1
  fi
  t1=$(date +%s%N)
  awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.3f", (b-a)/1e9}'
}

parse_ceres_iters() { awk '/^Minimizer iterations/ {print $NF; exit}' "$1"; }
parse_symforce_total() { awk -F'|' '/Optimizer<sym::Optimize>::Optimize/ {gsub(/ /,"",$3); print $3; exit}' "$1"; }
parse_symforce_iters() { awk '/LM<sym::Optimize> \[iter/ {n++} END{print (n=="")?"-":n}' "$1"; }
parse_symforce_cost()  { awk '/error prev\/linear\/new:/ {c=$0; sub(/.*prev\/linear\/new: /,"",c); split(c,a,","); split(a[1],b,"/"); v=b[3]} END{print (v=="")?"-":v}' "$1"; }
parse_ceres_cost()  { awk '/^Final[[:space:]]+[0-9]/ {print $2; exit}' "$1"; }
parse_gtsam_iters() { awk '/^iterations: / {print $2; exit}' "$1"; }
parse_gtsam_cost()  { awk '/^final error:/ {print $3; exit}' "$1"; }
parse_g2o_iters()   { awk '/^iteration=/ {n++} END{print (n=="")?"-":n}' "$1"; }
parse_g2o_cost()    { awk '/chi2=/ {for(i=1;i<=NF;i++) if ($i=="chi2=") c=$(i+1)} END{print (c=="")?"-":c}' "$1"; }

declare -a NAMES TIMES ITER_COUNTS COSTS LOGS

record() { # name time iters cost log
  NAMES+=("$1"); TIMES+=("$2"); ITER_COUNTS+=("$3"); COSTS+=("$4"); LOGS+=("$5")
}

# best_of <name> <command...> : runs REPEAT times, echoes "<fastest wall> <log>"
# for the fastest run. (Called via $(...), so nothing may rely on shared state.)
best_of() {
  local name="$1"; shift
  local i t best="" bestlog="" log
  for ((i = 1; i <= REPEAT; i++)); do
    log="$OUT_DIR/${tag}_${name}_run${i}.log"
    t=$(run_timed "$log" "$@") || return 1
    if [[ -z "$best" ]] || awk -v a="$t" -v b="$best" 'BEGIN{exit !(a<b)}'; then
      best="$t"; bestlog="$log"
    fi
  done
  echo "$best $bestlog"
}

echo "========== three-way BAL benchmark =========="
echo "[3way] dataset   : $DATASET"
echo "[3way] arch      : $(uname -m)"
echo "[3way] cpu       : $(awk -F: '/model name/ {print $2; exit}' /proc/cpuinfo | sed 's/^ //')"
echo "[3way] cores     : $(nproc)"
echo "[3way] affinity  : $(taskset -pc $$ 2>/dev/null | awk -F: '{gsub(/^ +/,"",$2); print $2; exit}')"
echo "[3way] iterations: ceres/g2o = $ITERS (engine default 5), gtsam = own convergence test"
echo "[3way] repeat    : $REPEAT"
echo

# --- Ceres -------------------------------------------------------------
out=$(best_of ceres_dense "$CERES_BIN" --input "$DATASET_PATH" -num_threads=1 -linear_solver=dense_schur)
record ceres_dense "${out%% *}" "$(parse_ceres_iters "${out##* }")" "$(parse_ceres_cost "${out##* }")" "${out##* }"

out=$(best_of ceres_sparse "$CERES_BIN" --input "$DATASET_PATH" -num_threads=1 -linear_solver=sparse_schur)
record ceres_sparse "${out%% *}" "$(parse_ceres_iters "${out##* }")" "$(parse_ceres_cost "${out##* }")" "${out##* }"

# --- GTSAM -------------------------------------------------------------
out=$(best_of gtsam_bal "$GTSAM_BAL_BIN" "$DATASET_PATH")
record gtsam_bal "${out%% *}" "$(parse_gtsam_iters "${out##* }")" "$(parse_gtsam_cost "${out##* }")" "${out##* }"

if [[ -x "$GTSAM_SMART_BIN" ]]; then
  out=$(best_of gtsam_smartfactor "$GTSAM_SMART_BIN" "$DATASET_PATH")
  record gtsam_smartfactor "${out%% *}" "$(parse_gtsam_iters "${out##* }")" "$(parse_gtsam_cost "${out##* }")" "${out##* }"
fi

# --- SymForce ----------------------------------------------------------
if [[ -x "$SYMFORCE_BIN" ]]; then
  out=$(best_of symforce "$SYMFORCE_BIN" "$DATASET_PATH")
  record symforce "${out%% *}" "$(parse_symforce_iters "${out##* }")" "$(parse_symforce_cost "${out##* }")" "${out##* }"
fi

# --- g2o ---------------------------------------------------------------
if [[ -x "$G2O_BAL_BIN" ]]; then
  out=$(best_of g2o_sparse "$G2O_BAL_BIN" -i "$ITERS" -v "$DATASET_PATH")
  record g2o_sparse "${out%% *}" "$(parse_g2o_iters "${out##* }")" "$(parse_g2o_cost "${out##* }")" "${out##* }"

  out=$(best_of g2o_pcg "$G2O_BAL_BIN" -i "$ITERS" -v -pcg "$DATASET_PATH")
  record g2o_pcg "${out%% *}" "$(parse_g2o_iters "${out##* }")" "$(parse_g2o_cost "${out##* }")" "${out##* }"
fi

# --- report ------------------------------------------------------------
{
  for i in "${!NAMES[@]}"; do
    printf '    ("%s", "%s", "%s", "%s"),\n' "${NAMES[$i]}" "${TIMES[$i]}" "${ITER_COUNTS[$i]}" "${COSTS[$i]}"
  done
} > "$OUT_DIR/${tag}_rows.py"

python3 - "$OUT_DIR/${tag}_rows.py" "$DATASET" <<'PY'
import sys

rows = []
with open(sys.argv[1]) as fh:
    for line in fh:
        line = line.strip()
        if line:
            rows.append(eval(line.rstrip(",")))

def table(headers, rows):
    widths = [max(len(str(headers[i])), *(len(str(r[i])) for r in rows)) for i in range(len(headers))]
    sep = "+" + "+".join("-" * (w + 2) for w in widths) + "+"
    def fmt(r):
        return "|" + "|".join(
            f" {str(r[i]):<{widths[i]}} " if i == 0 else f" {str(r[i]):>{widths[i]}} "
            for i in range(len(r))) + "|"
    return "\n".join([sep, fmt(headers), sep.replace("-", "=")] + [fmt(r) for r in rows] + [sep])

times = []
for _, t, _, _ in rows:
    try:
        times.append(float(t))
    except ValueError:
        pass
baseline = min(times) if times else None

out = []
for name, t, iters, cost in rows:
    try:
        tt = float(t)
    except ValueError:
        out.append((name, t, iters, "n/a", "n/a", cost))
        continue
    per_iter = tt / int(iters) if iters.isdigit() and int(iters) > 0 else 0.0
    rel = f"{tt / baseline:.2f}x" if baseline else "n/a"
    out.append((name, f"{tt:.3f}", iters, f"{per_iter:.4f}" if per_iter else "n/a", rel, cost))

print()
print("========== Summary [%s] ==========" % sys.argv[2])
print(table(["engine", "wall (s)", "iters", "per_iter (s)", "vs fastest", "final cost"], out))
PY

echo
echo "[3way] logs in: $OUT_DIR"

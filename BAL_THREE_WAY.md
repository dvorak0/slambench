# BAL comparison: Ceres / SymForce / GTSAM / g2o

Upstream issue: [borglab/gtsam#2299 - Significant runtime difference between CERES and GTSAM on same BAL problem](https://github.com/borglab/gtsam/issues/2299)

The existing benchmark covers SymForce / Ceres / GTSAM / MSCKF, but never g2o. This document fills that gap: **how long does each backend need on the same BAL problem**, and does the Ceres-GTSAM gap from the issue reproduce here.

## Environment

| Item | Value |
| --- | --- |
| Host | NixOS 25.11, Intel Core Ultra 9 275HX (24 cores), 30 GB RAM |
| Runtime | Docker `ubuntu:22.04`, gcc 11, CMake Release |
| Dataset | `problem-16-22106-pre.txt` (Dubrovnik: 16 cameras / 22106 points / 83718 observations) |
| Timing | Whole-process wall time (parse + optimize + teardown), best of 3 runs |
| Single-core setup | `--cpuset-cpus=0`, the same comparison basis as Ceres `-num_threads=1` in the issue |
| Robust kernel | None anywhere (Ceres default, SymForce example, GTSAM SFM examples and g2o `bal_example` all skip Huber), so costs are comparable |

Engine versions:

- Ceres 2.2.0 (`bundle_adjuster`, `-linear_solver=dense_schur` / `sparse_schur`)
- SymForce `f0e52ad` (`bundle_adjustment_in_the_large_example`, LM + sparse Cholesky)
- GTSAM: the fork pinned in this repo (`SFMExample_bal` / `SFMExample_SmartFactor_bal`), plus **upstream `develop`**'s `timing/timeSFMBAL` (which contains the point-batched multifrontal path from PR #2694)
- g2o `fd65627` (`g2o/examples/bal/bal_example`, LM + Schur complement). suitesparse 5.10 on Ubuntu 22.04 ships no CHOLMOD CMake config, so this build uses Eigen sparse Cholesky; a `-pcg` variant is measured as well.

## Result 1: single core (same comparison basis as the issue)

| engine | wall (s) | iters | per_iter (s) | vs fastest | final cost |
| --- | --- | --- | --- | --- | --- |
| ceres_dense | **0.364** | 6 | 0.0607 | 1.00x | 1.803391e+04 |
| ceres_sparse | 0.404 | 6 | 0.0673 | 1.11x | 1.803391e+04 |
| symforce | 1.680 | 11 | 0.1527 | 4.62x | 1.803e+04 |
| **gtsam_bal** (`SFMExample_bal`) | **2.391** | 8 | 0.2989 | 6.57x | 20475.2566825 |
| gtsam_smartfactor | 0.885 | 4 | 0.2213 | 2.43x | 21342.6200134 |
| g2o_sparse (Eigen, 5 iterations) | 0.437 | 5 | 0.0874 | 1.20x | 7.469e+05 (not converged) |
| g2o_pcg | 0.437 | 5 | 0.0874 | 1.20x | 7.469e+05 (not converged) |

Note: g2o's `bal_example` starts from `lambda = 7.1e4` and divides it by 3 per step, so five iterations barely move the solution. Running it to convergence:

| engine | wall (s) | iters | note |
| --- | --- | --- | --- |
| g2o_sparse `-i 20` | 1.29 | 20 (stable at 19) | chi2 settles at 36067.8 |
| g2o parse/init overhead `-i 0` | 0.11 | 0 | reads a 4.8 MB text file |

g2o's converged chi2 `36067.8` is exactly 2x Ceres' cost `18033.9` (Ceres reports `0.5*sum(r^2)`), i.e. both reach the **same solution**, differing only by the cost convention.

**Takeaways**

- The issue's core observation reproduces: on the same BAL problem `SFMExample_bal` is **6.6x** slower than Ceres (the issue reported 3.8x with TBB and 8.3x without - same order of magnitude).
- SymForce (1.68 s, 11 iterations) is 4.6x slower than Ceres, in the same league as the classic GTSAM path; per iteration (0.153 s) it is 2x faster than the classic GTSAM path.
- g2o has the cheapest iteration (0.087 s/it, 3.4x faster than the classic GTSAM path), but because of its conservative LM lambda initialisation **reaching the same accuracy actually costs 1.29 s**.
- The repo's own `SmartFactor_bal` example already brings 2.39 s down to 0.885 s.

## Result 2: all cores (24, GTSAM uses TBB)

| engine | wall (s) | iters | per_iter (s) | single -> all cores |
| --- | --- | --- | --- | --- |
| ceres_dense | 0.452 | 6 | 0.0753 | 0.36 -> 0.45 (single-threaded, noise) |
| ceres_sparse | 0.612 | 6 | 0.1020 | 0.40 -> 0.61 |
| symforce | 1.656 | 11 | 0.1505 | 1.68 -> 1.66 (single-threaded) |
| gtsam_bal | 1.722 | 8 | 0.2152 | 2.39 -> 1.72 (1.4x) |
| gtsam_smartfactor | **0.370** | 4 | 0.0925 | 0.89 -> 0.37 (2.4x) |
| g2o_sparse | 0.446 | 5 | 0.0892 | 0.44 -> 0.45 (single-threaded) |
| g2o_pcg | 0.445 | 5 | 0.0890 | 0.44 -> 0.45 (single-threaded) |

TBB is only worth 1.4x for the classic `SFMExample_bal`, consistent with the issue's own measurement (4.894 s -> 3.145 s, 1.6x).

## Result 3: upstream GTSAM's new path (`timing/timeSFMBAL`, single core)

`borglab/gtsam` `develop`, 5 iterations, cost again converging to `1.8e+04`:

| path | wall (s) |
| --- | --- |
| Regular Cholesky | 1.060 |
| Regular Solver | 1.388 |
| Regular MultifrontalQR | 7.023 |
| Batch Cholesky | 0.984 |
| Batch QR | 7.129 |
| **BatchFactor / MultifrontalSolver** | **0.648** |

That is the point-batched multifrontal path recommended in the issue's conclusion (PR #2694): a **2.14x** speedup (1.388 -> 0.648 s). On a single core it already beats g2o's 5-iteration result, but is still slower than Ceres' dense Schur (0.364 s).

> Caveat: suitesparse 5.10 on Ubuntu 22.04 provides no `CHOLMODConfig.cmake`, so neither g2o nor GTSAM enables CHOLMOD in this round (GTSAM's `--point-batch-schur-cholmod-only` aborts with *no CHOLMOD support*). `.devcontainer/CHOLMODConfig.cmake` (a minimal imported target) is in the repo; enabling it is the next step - the expectation is roughly 0.25 s for GTSAM batch + CHOLMOD, and g2o on CHOLMOD/METIS.

## Appendix: full backend harness (`run_backend.sh`, single core)

`run_bal_three_way.sh` covers the four BAL solvers plus g2o; the repo's original harness also runs the MSCKF variants, recorded here for completeness:

| engine | total (s) | iters | per_iter (s) | final cost |
| --- | --- | --- | --- | --- |
| ceres_dense | 0.331 | 6 | 0.055 | 1.803e+04 |
| ceres_sparse | 0.343 | 6 | 0.057 | 1.803e+04 |
| symforce | 1.550 | 12 | 0.129 | 1.803e+04 |
| gtsam_bal | 2.520 | 8 | 0.315 | 2.048e+04 |
| gtsam_smartfactor | 0.924 | 4 | 0.231 | 2.134e+04 |
| msckf_rr | 2.194 | 6 | 0.366 | 1.803e+04 |
| msckf_rc | 5.470 | 6 | 0.912 | 1.803e+04 |
| msckf_cr | 4.686 | 6 | 0.781 | 1.803e+04 |
| msckf_cc | 6.416 | 6 | 1.069 | 1.803e+04 |
| msckf_openblas | 4.323 | 6 | 0.721 | 1.803e+04 |

## How to reproduce

```bash
# multi-engine comparison (single core)
docker build -f .devcontainer/Dockerfile -t slambench:bal-g2o .
docker run --rm --cpuset-cpus=0 \
  -v $PWD/data:/workspace/data -v $PWD/three_way_out:/workspace/three_way_out \
  -v $PWD/run_bal_three_way.sh:/workspace/run_bal_three_way.sh:ro \
  --entrypoint /bin/bash slambench:bal-g2o \
  -lc "THREE_WAY_REPEAT=3 /workspace/run_bal_three_way.sh problem-16-22106-pre.txt"

# full harness (including symforce / msckf)
docker run --rm --cpuset-cpus=0 -v $PWD/data:/workspace/data \
  --entrypoint /bin/bash slambench:bal-g2o \
  -lc "/workspace/run_backend.sh problem-16-22106-pre.txt"

# g2o alone
docker run --rm --cpuset-cpus=0 --entrypoint /bin/bash slambench:bal-g2o \
  -lc "/workspace/run_g2o.sh 20 /tmp/g2o.log"

# upstream GTSAM's new path
docker build -f .devcontainer/Dockerfile.gtsam-upstream -t slambench:gtsam-upstream .
docker run --rm --cpuset-cpus=0 -v $PWD/data:/workspace/data \
  --entrypoint /bin/bash slambench:gtsam-upstream \
  -lc "/opt/slambench/gtsam-upstream/build/timing/timeSFMBAL /workspace/data/dubrovnik/problem-16-22106-pre.txt"
```

## Follow-ups

1. Enable CHOLMOD for g2o and GTSAM (`CHOLMOD_DIR=/opt/slambench/.../cholmod-cmake`) and re-measure.
2. Give g2o a smaller LM lambda initial value (`setUserLambdaInit`) and report a tuned g2o next to the stock one.
3. Extend to BAL-88 / BAL-135 to see how the gap scales with problem size.

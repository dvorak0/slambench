# BAL 三方对比：Ceres vs GTSAM vs g2o

对应 issue：[borglab/gtsam#2299 — Significant runtime difference between CERES and GTSAM on same BAL problem](https://github.com/borglab/gtsam/issues/2299)

本文档是旧 benchmark（Ceres / GTSAM / SymForce / MSCKF）在 BAL 上的延伸：**加入 g2o**，把「同一个 BAL 问题，三个主流后端各要多久」测清楚，并顺带复现 issue 里的 Ceres–GTSAM 差距。

## 环境

| 项 | 值 |
| --- | --- |
| 宿主机 | NixOS 25.11，Intel Core Ultra 9 275HX（24 核），30 GB RAM |
| 运行环境 | Docker `ubuntu:22.04`，gcc 11，CMake Release |
| 数据集 | `problem-16-22106-pre.txt`（Dubrovnik，16 相机 / 22106 点 / 83718 观测） |
| 计时方式 | 进程整体 wall time（解析 + 优化 + 收尾），取 3 次最快值 |
| 单核配置 | `--cpuset-cpus=0`，等价于 issue 里 Ceres `-num_threads=1` |
| 鲁棒核 | 三方均为**无**（Ceres 默认、GTSAM SFM 示例、g2o `bal_example` 都不加 Huber），成本可比 |

引擎版本：

- Ceres 2.2.0（`bundle_adjuster`，`-linear_solver=dense_schur` / `sparse_schur`）
- GTSAM：仓库 pin 的 fork（`SFMExample_bal` / `SFMExample_SmartFactor_bal`），以及 **upstream develop** 的 `timing/timeSFMBAL`（含 PR #2694 的 point-batch multifrontal 路径）
- g2o `fd65627`（`g2o/examples/bal/bal_example`，LM + Schur complement；本机 suitesparse 5.10 无 CHOLMOD CMake config，故走 Eigen 稀疏 Cholesky，另有 `-pcg` 变体）

## 结果 1：单核（1 core，与 issue 同一比较口径）

| engine | wall (s) | iters | per_iter (s) | vs 最快 | final cost |
| --- | --- | --- | --- | --- | --- |
| ceres_dense | 0.371 | 6 | 0.0618 | 1.00x | 1.803391e+04 |
| ceres_sparse | 0.404 | 6 | 0.0673 | 1.09x | 1.803391e+04 |
| **gtsam_bal**（SFMExample_bal） | **2.400** | 8 | 0.3000 | 6.47x | 20475.2566825 |
| gtsam_smartfactor | 0.888 | 4 | 0.2220 | 2.39x | 21342.6200134 |
| g2o_sparse（Eigen，5 iter） | 0.436 | 5 | 0.0872 | 1.18x | 7.469e+05（未收敛） |
| g2o_pcg | 0.438 | 5 | 0.0876 | 1.18x | 7.469e+05（未收敛） |

⚠️ g2o 的 `bal_example` 默认 `lambda = 7.1e4` 起跳、每步 /3，5 次迭代几乎没走到解。跑满到收敛：

| engine | wall (s) | iters | 说明 |
| --- | --- | --- | --- |
| g2o_sparse `-i 20` | 1.29 | 20（第 19 次到位） | chi2 稳定在 36067.8 |
| g2o 解析/初始化开销 `-i 0` | 0.11 | 0 | 读 4.8 MB 文本 |

g2o 收敛值 `36067.8` 正好是 Ceres `cost = 18033.9` 的 2 倍（Ceres 的 cost 是 `0.5·Σr²`），说明两者求到的是**同一个解**，只是成本定义差一个 1/2。

**解读**

- 复现了 issue 的核心现象：同一 BAL 问题，`SFMExample_bal` 比 Ceres 慢 **6.5x**（issue 原文 3.8x w/ TBB、8.3x w/o TBB，同一量级）。
- g2o 单次迭代（0.087 s）比 GTSAM 经典路径（0.305 s）快 2.7x，但因为 LM lambda 初始化太保守，**达到同精度反而要 1.29 s**（Cerces 的 3.4x）。
- GTSAM 的 `SmartFactor_bal`（本项目自己加的示例）已经把 2.40 s 压到 0.888 s。

## 结果 2：全核（24 核，GTSAM 走 TBB）

| engine | wall (s) | iters | per_iter (s) | 单核 → 全核 |
| --- | --- | --- | --- | --- |
| ceres_dense | 0.416 | 6 | 0.0693 | 0.37 → 0.42（单线程，噪声） |
| ceres_sparse | 0.592 | 6 | 0.0987 | 0.40 → 0.59 |
| gtsam_bal | 1.689 | 8 | 0.2111 | 2.40 → 1.69（1.4x） |
| gtsam_smartfactor | 0.377 | 4 | 0.0943 | 0.89 → 0.38（2.4x） |
| g2o_sparse | 0.442 | 5 | 0.0884 | 0.44 → 0.44（单线程） |
| g2o_pcg | 0.439 | 5 | 0.0878 | 0.44 → 0.44（单线程） |

TBB 对经典 `SFMExample_bal` 只值 1.4x，和 issue 里 @dvorak0 的观察一致（4.894 → 3.145 仅 1.6x）。

## 结果 3：upstream GTSAM 的新路径（`timing/timeSFMBAL`，单核）

`borglab/gtsam` develop，5 次迭代，成本同样收敛到 `1.8e+04`：

| 路径 | wall (s) |
| --- | --- |
| Regular Cholesky | 1.060 |
| Regular Solver | 1.388 |
| Regular MultifrontalQR | 7.023 |
| Batch Cholesky | 0.984 |
| Batch QR | 7.129 |
| **BatchFactor / MultifrontalSolver** | **0.648** |

即 issue 结论里推荐的 point-batch multifrontal 路径（PR #2694）：**2.14x** 加速（1.388 → 0.648 s），单核下已经优于 g2o 的 5 次迭代结果，但仍慢于 Ceres 的 dense Schur（0.383 s）。

> 注意：本机 Ubuntu 22.04 的 suitesparse 5.10 不提供 `CHOLMODConfig.cmake`，所以 g2o 与 GTSAM 都没启用 CHOLMOD（GTSAM 的 `--point-batch-schur-cholmod-only` 直接报 *no CHOLMOD support*）。仓库里已放 `.devcontainer/CHOLMODConfig.cmake`（最小 imported target），启用后预计 GTSAM batch + CHOLMOD 可到 ~0.25 s、g2o 也能走 CHOLMOD/METIS——留作下一步。

## 复现方式

```bash
# 三方对比（单核）
docker build -f .devcontainer/Dockerfile -t slambench:bal-g2o .
docker run --rm --cpuset-cpus=0 \
  -v $PWD/data:/workspace/data -v $PWD/three_way_out:/workspace/three_way_out \
  -v $PWD/run_bal_three_way.sh:/workspace/run_bal_three_way.sh:ro \
  --entrypoint /bin/bash slambench:bal-g2o \
  -lc "THREE_WAY_REPEAT=3 /workspace/run_bal_three_way.sh problem-16-22106-pre.txt"

# 单独跑 g2o
docker run --rm --cpuset-cpus=0 --entrypoint /bin/bash slambench:bal-g2o \
  -lc "/workspace/run_g2o.sh 20 /tmp/g2o.log"

# upstream GTSAM 新路径
docker build -f .devcontainer/Dockerfile.gtsam-upstream -t slambench:gtsam-upstream .
docker run --rm --cpuset-cpus=0 -v $PWD/data:/workspace/data \
  --entrypoint /bin/bash slambench:gtsam-upstream \
  -lc "/opt/slambench/gtsam-upstream/build/timing/timeSFMBAL /workspace/data/dubrovnik/problem-16-22106-pre.txt"
```

## 待办

1. 启用 CHOLMOD（g2o + GTSAM）：`CHOLMOD_DIR=/opt/slambench/.../cholmod-cmake`，再测一轮。
2. g2o 用小 `lambda` 初值（`setUserLambdaInit`）再测，得到「调优后的 g2o」。
3. 扩展到 BAL-88 / BAL-135，看差距随问题规模的走势。

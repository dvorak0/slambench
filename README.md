# SymForce BAL Example (Devcontainer)

This workspace is set up to run SymForce’s `bundle_adjustment_in_the_large` example in a devcontainer per [containers.dev](https://containers.dev/).

## Repo checkout
- This repo uses a submodule for `symforce`. After cloning, run:
  ```
  git submodule update --init --recursive
  ```

## Devcontainer
- Base image: `ubuntu:22.04` with system Python 3.10, build tools. Dockerfile builds and installs `symforce` from the submodule at `/opt/symforce`, and pre-builds the `bundle_adjustment_in_the_large_example` binary. It also creates a non-root user `vscode` (uid/gid 1000) used by the devcontainer.
- Build image: `docker build -t symforce-dev -f .devcontainer/Dockerfile .`
- Start container: `docker run -it --rm -v "$PWD":/workspace -w /workspace symforce-dev bash`
  - Or open the folder in VS Code with the Dev Containers extension and “Reopen in Container”.

## One-shot run (inside container)
1) Ensure submodule is present: `git submodule update --init --recursive`
2) SymForce run: `./run_symforce.sh`
   - Expects dataset at `data/dubrovnik/problem-16-22106-pre.txt` (committed to this repo)
   - Runs prebuilt `/opt/symforce/build/bin/examples/bundle_adjustment_in_the_large_example` with that dataset
   - Tees output to `bundle_adjustment_dubrovnik.log`
3) Ceres run: `./run_ceres.sh`
   - Uses same dataset path
   - Runs prebuilt `/opt/ceres-solver/build/bin/bundle_adjuster` with `--input <dataset> -num_threads=1 -linear_solver=dense_schur`
   - Tees output to `bundle_adjustment_dubrovnik_ceres.log`
4) Benchmark both and summarize: `./run_benchmarks.sh`
   - Runs both scripts and parses total times from the logs
   - Prints symforce time, ceres time, and the ratio

## Notes
- Container image tag used above: `symforce-dev`.
- Data files download into `symforce/symforce/examples/bundle_adjustment_in_the_large/data/dubrovnik/`.

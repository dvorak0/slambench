# SymForce BAL Example (Devcontainer)

This workspace is set up to run SymForce’s `bundle_adjustment_in_the_large` example in a devcontainer per [containers.dev](https://containers.dev/).

## Build & run (no volume mount needed)
1) Build image: `docker build -t symforce-dev -f .devcontainer/Dockerfile .`
2) Run benchmark inside the image:  
   `docker run --rm symforce-dev bash -lc "./run_benchmarks.sh"`
   - Uses bundled dataset at `data/dubrovnik/problem-16-22106-pre.txt`
   - Runs prebuilt SymForce and Ceres binaries in `/opt/symforce` and `/opt/ceres-solver`
   - Writes logs to `/workspace/bundle_adjustment_dubrovnik*.log` inside the container

If you want the logs on the host, add a bind mount:  
`docker run --rm -v "$PWD":/workspace symforce-dev bash -lc "./run_benchmarks.sh"`

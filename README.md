# SlamBench BAL Benchmarks

Run SymForce, Ceres, GTSAM, and a minimal C MSCKF-style benchmark in one container. Everything needed (code, binaries, dataset) is baked into the image; mount the workspace only if you want the logs on the host.

```
git clone --recursive git@github.com:dvorak0/slambench.git
cd slambench
docker build -t slambench-dev -f .devcontainer/Dockerfile .
docker run --rm --cpuset-cpus="0" slambench-dev
```

Note: if you want the logs/results on the host, mount the workspace:  
`docker run --rm --cpuset-cpus="0" -v "$PWD":/workspace slambench-dev`

To drop into a shell instead of running the benchmarks, override the entrypoint:
`docker run --rm -it --entrypoint bash slambench-dev`

Outputs (saved in the container or mounted workspace):
- `symforce.log`, `ceres.log`, `gtsam.log`, `msckf.log`

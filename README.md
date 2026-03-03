# SlamBench BAL Benchmarks

Run both SymForce and Ceres benchmarks in one container. Everything needed (code, binaries, dataset) is baked into the image; mount the workspace only if you want the logs on the host.

```
git clone --recursive git@github.com:dvorak0/slambench.git
cd slambench
docker build -t slambench-dev -f .devcontainer/Dockerfile .
docker run --rm slambench-dev
```

Note: if you want the logs/results on the host, mount the workspace:
`docker run --rm -v "$PWD":/workspace slambench-dev`

To drop into a shell instead of running the benchmarks, override the entrypoint:
`docker run --rm -it --entrypoint bash slambench-dev`

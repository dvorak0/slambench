#!/bin/bash
# Build AOT Harris Generator - produces static library
# No external GenGen.cpp needed (inlined)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HALIDE_ROOT=/usr/local/lib/python3.10/dist-packages/halide
export LD_LIBRARY_PATH=$HALIDE_ROOT/lib64:$LD_LIBRARY_PATH

cd $SCRIPT_DIR

echo "=========================================="
echo "HALIDE AOT Harris Generator"
echo "=========================================="

# Step 1: Compile generator (no GenGen.cpp needed)
echo "[1/3] Compiling Harris generator..."
g++ harris_generator.cpp \
    -g -std=c++17 -fno-rtti \
    -I $HALIDE_ROOT/include \
    -L $HALIDE_ROOT/lib64 \
    -lHalide -lpthread -ldl \
    -o harris_generator

# Step 2: Generate AOT (manual schedule)
echo "[2/3] Generating AOT (manual schedule)..."
./harris_generator \
    -o . \
    -g harris \
    -f harris_manual \
    -e static_library,h,schedule \
    target=host \
    generator_mode=1

echo "Generated: harris_manual.a, harris_manual.h"

# Step 3: Generate AOT (auto schedule)
echo "[3/3] Generating AOT (auto schedule)..."
./harris_generator \
    -o . \
    -g harris \
    -f harris_auto \
    -e static_library,h,schedule \
    -p $HALIDE_ROOT/lib64/libautoschedule_mullapudi2016.so \
    target=host \
    generator_mode=0 \
    autoscheduler=Mullapudi2016 \
    autoscheduler.parallelism=1 \
    autoscheduler.last_level_cache_size=1024000 \
    autoscheduler.balance=40

echo "Generated: harris_auto.a, harris_auto.h"

echo "=========================================="
echo "Done!"
echo "=========================================="

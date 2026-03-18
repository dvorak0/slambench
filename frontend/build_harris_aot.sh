#!/bin/bash
# Build AOT Harris Generator - produces static library
# Output: harris_manual.a, harris_manual.h, harris_auto.a, harris_auto.h

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HALIDE_ROOT=/usr/local/lib/python3.10/dist-packages/halide
HALIDE_SRC=/halide

cd $SCRIPT_DIR

echo "=========================================="
echo "HALIDE AOT Harris Generator"
echo "=========================================="

# Step 1: Compile generator
echo "[1/5] Compiling Harris generator..."
g++ harris_generator.cpp \
    $HALIDE_SRC/tools/GenGen.cpp \
    -g -std=c++17 -fno-rtti \
    -I $HALIDE_ROOT/include \
    -I $HALIDE_SRC/src \
    -L $HALIDE_ROOT/lib64 \
    -lHalide -lpthread -ldl \
    -o harris_generator

# Step 2: Generate AOT (no schedule - baseline)
echo "[2/5] Generating AOT (no schedule)..."
./harris_generator \
    -o . \
    -g harris \
    -f harris_none \
    -e static_library,h,schedule \
    target=host

echo "Generated: harris_none.a, harris_none.h"

# Step 3: Generate AOT (manual schedule via command line)
echo "[3/5] Generating AOT (manual schedule)..."
./harris_generator \
    -o . \
    -g harris \
    -f harris_manual \
    -e static_library,h,schedule \
    target=host \
    -s "Var yi; output.split(y, y, yi, 32).parallel(y).vectorize(x, 8)"

echo "Generated: harris_manual.a, harris_manual.h"

# Step 4: Generate AOT (auto schedule)
echo "[4/5] Generating AOT (auto schedule)..."
./harris_generator \
    -o . \
    -g harris \
    -f harris_auto \
    -e static_library,h,schedule \
    -p $HALIDE_ROOT/lib64/libautoschedule_mullapudi2016.so \
    target=host \
    autoscheduler=Mullapudi2016 \
    autoscheduler.parallelism=32 \
    autoscheduler.last_level_cache_size=16777216 \
    autoscheduler.balance=40

echo "Generated: harris_auto.a, harris_auto.h"

echo "[5/5] Done!"
echo "=========================================="

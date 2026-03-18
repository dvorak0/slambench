#!/bin/bash
# Build AOT Harris Generator - produces static library
# Output: harris_manual.a, harris_manual.h

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HALIDE_ROOT=/usr/local/lib/python3.10/dist-packages/halide
HALIDE_SRC=/halide

cd $SCRIPT_DIR

echo "=========================================="
echo "HALIDE AOT Harris Generator"
echo "=========================================="

# Step 1: Compile generator
echo "[1/2] Compiling Harris generator..."
g++ harris_generator.cpp \
    $HALIDE_SRC/tools/GenGen.cpp \
    -g -std=c++17 -fno-rtti \
    -I $HALIDE_ROOT/include \
    -I $HALIDE_SRC/src \
    -L $HALIDE_ROOT/lib64 \
    -lHalide -lpthread -ldl \
    -o harris_generator

# Step 2: Generate AOT (manual schedule)
echo "[2/2] Generating AOT (manual schedule)..."
./harris_generator \
    -o . \
    -g harris \
    -f harris_manual \
    -e static_library,h,schedule \
    target=host \
    generator_mode=1

echo "Generated: harris_manual.a, harris_manual.h"

echo "=========================================="
echo "Done!"
echo "=========================================="

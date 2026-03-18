#!/bin/bash
# Build AOT-compiled Harris using Generator

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HALIDE_ROOT=/usr/local/lib/python3.10/dist-packages/halide
HALIDE_SRC=/halide

cd $SCRIPT_DIR

echo "=========================================="
echo "HALIDE AOT Harris Builder (Generator)"
echo "=========================================="

# Step 1: Compile generator
echo "[1/3] Compiling Harris generator..."
g++ harris_generator.cpp \
    $HALIDE_SRC/tools/GenGen.cpp \
    -g -std=c++17 -fno-rtti \
    -I $HALIDE_ROOT/include \
    -I $HALIDE_SRC/src \
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
    target=host

echo "Generated: harris_manual.a, harris_manual.h"

# Step 3: Build test runner
echo "[3/3] Building test runner..."

cat > harris_aot_test.cpp << 'EOF'
#include "harris_manual.h"
#include <opencv2/opencv.hpp>
#include <iostream>
#include <chrono>

using namespace Halide;
using namespace std;
using namespace std::chrono;

int main(int argc, char** argv) {
    const char* input_path = argv[1] ? argv[1] : "/workspace/data/euroc/frame0.png";
    
    cv::Mat img = cv::imread(input_path, cv::IMREAD_GRAYSCALE);
    if (img.empty()) {
        cerr << "Failed to load: " << input_path << endl;
        return 1;
    }
    
    int W = img.cols;
    int H = img.rows;
    cout << "Image: " << W << "x" << H << endl;
    
    Buffer<uint8_t, 2> input(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            input(x, y) = img.at<uint8_t>(y, x);
        }
    }
    
    Buffer<float, 2> output(W, H);
    
    // Call AOT function
    harris_manual(input, output);
    
    // Time it
    const int runs = 5;
    double total = 0;
    for (int i = 0; i < runs; i++) {
        auto start = high_resolution_clock::now();
        harris_manual(input, output);
        auto end = high_resolution_clock::now();
        total += duration_cast<microseconds>(end - start).count() / 1000.0;
    }
    
    cout << "Harris (manual AOT): " << (total / runs) << " ms" << endl;
    
    return 0;
}
EOF

g++ harris_aot_test.cpp \
    harris_manual.a \
    -std=c++17 \
    -I $HALIDE_ROOT/include \
    -I $HALIDE_SRC \
    -I /usr/include/opencv4 \
    -ldl -lpthread \
    -lopencv_core -lopencv_imgcodecs \
    -o harris_aot_test

echo "Built: harris_aot_test"

echo ""
echo "Running test..."
./harris_aot_test /workspace/data/euroc/frame0.png

echo ""
echo "=========================================="
echo "Done!"
echo "=========================================="

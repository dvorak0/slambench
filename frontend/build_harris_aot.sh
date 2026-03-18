#!/bin/bash
# Build AOT-compiled Harris
# Based on the successful nixos workflow

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HALIDE_ROOT=${HALIDE_ROOT:-$(python3 -c "import halide; print(halide.install_dir())")}
HALIDE_SRC=${HALIDE_SRC:-/tmp/Halide}

echo "=========================================="
echo "HALIDE AOT Harris Builder"
echo "=========================================="
echo "HALIDE_ROOT: $HALIDE_ROOT"
echo "Working dir: $SCRIPT_DIR"
echo ""

# Step 1: Get Halide source if not present
if [ ! -d "$HALIDE_SRC" ]; then
    echo "[1/6] Cloning Halide source..."
    git clone --depth 1 https://github.com/halide/Halide.git $HALIDE_SRC
else
    echo "[1/6] Halide source already present"
fi

cd $SCRIPT_DIR

# Step 2: Compile the Harris generator
echo "[2/6] Compiling Harris generator..."
g++ harris_generator.cpp \
    $HALIDE_SRC/src/GenGen.cpp \
    $HALIDE_SRC/src/Generator.h \
    -g -std=c++17 -fno-rtti \
    -I $HALIDE_ROOT/include \
    -L $HALIDE_ROOT/lib64 \
    -lHalide -lpthread -ldl \
    -o harris_generator

echo "Generator compiled: harris_generator"

# Step 3: Generate AOT with manual schedule
echo "[3/6] Generating AOT (manual schedule)..."
./harris_generator \
    -o . \
    -g harris \
    -f harris_manual \
    -e static_library,h,schedule \
    target=host

echo "Generated: harris_manual.a"

# Step 4: Generate AOT with auto schedule
echo "[4/6] Generating AOT (auto schedule)..."
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

echo "Generated: harris_auto.a"

# Step 5: Build test runner
echo "[5/6] Building test runner..."

# Create a simple test runner
cat > harris_test.cpp << 'EOF'
#include <Halide.h>
#include <iostream>
#include <chrono>
#include <opencv2/opencv.hpp>

using namespace Halide;
using namespace std;
using namespace std::chrono;

int main(int argc, char** argv) {
    const char* input_path = argv[1] ? argv[1] : "/workspace/data/euroc/frame0.png";
    
    // Load image
    cv::Mat img = cv::imread(input_path, cv::IMREAD_GRAYSCALE);
    if (img.empty()) {
        cerr << "Failed to load: " << input_path << endl;
        return 1;
    }
    
    // Convert to Buffer
    Buffer<uint8_t> input(img.cols, img.rows);
    for (int y = 0; y < img.rows; y++) {
        for (int x = 0; x < img.cols; x++) {
            input(x, y) = img.at<uint8_t>(y, x);
        }
    }
    
    Buffer<float> output(img.cols, img.rows);
    
    // Call the AOT-compiled function
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

g++ harris_test.cpp \
    harris_manual.a \
    -std=c++17 \
    -I $HALIDE_ROOT/include \
    -I $HALIDE_SRC \
    -ldl -lpthread \
    -o harris_test

echo "Test runner built: harris_test"

# Step 6: Run test
echo "[6/6] Running test..."
echo ""

if [ -f "/workspace/data/euroc/frame0.png" ]; then
    ./harris_test /workspace/data/euroc/frame0.png
else
    # Try without args
    ./harris_test
fi

echo ""
echo "=========================================="
echo "Done!"
echo "=========================================="

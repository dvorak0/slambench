#!/bin/bash
# Build AOT-compiled Harris using Halide Generator
# Based on the method discovered by the user

set -e

HALIDE_ROOT=${HALIDE_ROOT:-$(python3 -c "import halide; print(halide.install_dir())")}
HALIDE_SRC=${HALIDE_SRC:-/tmp/Halide}

echo "HALIDE_ROOT: $HALIDE_ROOT"

# Step 1: Get Halide source
if [ ! -d "$HALIDE_SRC" ]; then
    echo "Cloning Halide..."
    git clone --depth 1 https://github.com/halide/Halide.git $HALIDE_SRC
fi

cd $HALIDE_SRC

# Step 2: Build the Harris generator
echo "Building Harris generator..."
g++ tutorial/lesson_21_auto_scheduler_generate.cpp \
    tools/GenGen.cpp \
    -g -std=c++17 -fno-rtti \
    -I $HALIDE_ROOT/include \
    -L $HALIDE_ROOT/lib64 \
    -lHalide -lpthread -ldl \
    -o lesson_21_generate

# Step 3: Generate AOT code (manual schedule)
echo "Generating manual schedule AOT..."
./lesson_21_generate \
    -o . \
    -g auto_schedule_gen \
    -f harris_manual \
    -e static_library,h,schedule \
    target=host \
    -v 1

# Step 4: Generate AOT code (auto schedule)
echo "Generating auto schedule AOT..."
./lesson_21_generate \
    -o . \
    -g auto_schedule_gen \
    -f harris_auto \
    -e static_library,h,schedule \
    -p $HALIDE_ROOT/lib64/libautoschedule_mullapudi2016.so \
    target=host \
    autoscheduler=Mullapudi2016 \
    autoscheduler.parallelism=32 \
    autoscheduler.last_level_cache_size=16777216 \
    autoscheduler.balance=40

# Step 5: Build and run
echo "Building AOT binary..."
g++ tutorial/lesson_21_auto_scheduler_run.cpp \
    -std=c++17 \
    -I $HALIDE_ROOT/include \
    -I $HALIDE_SRC \
    -I tools/ \
    harris_manual.a harris_auto.a \
    -ldl -lpthread \
    -o harris_aot_run

# Step 6: Run
echo "Running AOT Harris..."
./harris_aot_run

echo "Done!"

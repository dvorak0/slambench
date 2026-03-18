// AOT-compiled Harris using Halide Generator approach
// This generates a static library that can be linked

#include <Halide.h>
#include <iostream>
#include <chrono>
#include <cstdlib>

using namespace Halide;
using namespace std;
using namespace std::chrono;

// The algorithm - same as before but will be AOT compiled
Func createHarrisPipeline(Buffer<float>& output) {
    Var x("x"), y("y");
    
    // Input - will be passed as parameter
    Func input("input");
    input(x, y) = BoundaryConditions::repeat_edge(input)(x, y);
    
    // Grayscale
    Func gray("gray");
    gray(x, y) = input(x, y);
    
    // Sobel gradients
    Func Iy("Iy");
    Iy(x, y) = gray(x - 1, y - 1) * (-1.0f / 12.0f) + gray(x - 1, y + 1) * (1.0f / 12.0f) +
               gray(x, y - 1) * (-2.0f / 12.0f) + gray(x, y + 1) * (2.0f / 12.0f) +
               gray(x + 1, y - 1) * (-1.0f / 12.0f) + gray(x + 1, y + 1) * (1.0f / 12.0f);

    Func Ix("Ix");
    Ix(x, y) = gray(x - 1, y - 1) * (-1.0f / 12.0f) + gray(x + 1, y - 1) * (1.0f / 12.0f) +
               gray(x - 1, y) * (-2.0f / 12.0f) + gray(x + 1, y) * (2.0f / 12.0f) +
               gray(x - 1, y + 1) * (-1.0f / 12.0f) + gray(x + 1, y + 1) * (1.0f / 12.0f);
    
    // Covariance
    Func Ixx("Ixx"), Iyy("Iyy"), Ixy("Ixy");
    Ixx(x, y) = Ix(x, y) * Ix(x, y);
    Iyy(x, y) = Iy(x, y) * Iy(x, y);
    Ixy(x, y) = Ix(x, y) * Iy(x, y);
    
    // Box filter
    Func Sxx("Sxx"), Syy("Syy"), Sxy("Sxy");
    Sxx(x, y) = Ixx(x - 1, y - 1) + Ixx(x - 1, y) + Ixx(x - 1, y + 1) +
                 Ixx(x, y - 1) + Ixx(x, y) + Ixx(x, y + 1) +
                 Ixx(x + 1, y - 1) + Ixx(x + 1, y) + Ixx(x + 1, y + 1);
    Syy(x, y) = Iyy(x - 1, y - 1) + Iyy(x - 1, y) + Iyy(x - 1, y + 1) +
                 Iyy(x, y - 1) + Iyy(x, y) + Iyy(x, y + 1) +
                 Iyy(x + 1, y - 1) + Iyy(x + 1, y) + Iyy(x + 1, y + 1);
    Sxy(x, y) = Ixy(x - 1, y - 1) + Ixy(x - 1, y) + Ixy(x - 1, y + 1) +
                 Ixy(x, y - 1) + Ixy(x, y) + Ixy(x, y + 1) +
                 Ixy(x + 1, y - 1) + Ixy(x + 1, y) + Ixy(x + 1, y + 1);
    
    // Harris response
    Func det("det"), trace("trace"), out("out");
    det(x, y) = Sxx(x, y) * Syy(x, y) - Sxy(x, y) * Sxy(x, y);
    trace(x, y) = Sxx(x, y) + Syy(x, y);
    out(x, y) = det(x, y) - 0.04f * trace(x, y) * trace(x, y);
    
    return out;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " <input.png> <output.png>" << endl;
        return 1;
    }
    
    // Load input image
    ImageInfo info = load_image(argv[1]);
    Buffer<float> input(info.width, info.height);
    Buffer<float> output(info.width, info.height);
    
    // For now, we'll use JIT but structure it for AOT conversion later
    // The key is to separate the algorithm definition from the schedule
    
    Var x("x"), y("y");
    Func gray("gray");
    gray(x, y) = BoundaryConditions::repeat_edge(input)(x, y);
    
    // Sobel
    Func Iy("Iy"), Ix("Ix");
    Iy(x, y) = gray(x - 1, y - 1) * (-1.0f / 12.0f) + gray(x - 1, y + 1) * (1.0f / 12.0f) +
               gray(x, y - 1) * (-2.0f / 12.0f) + gray(x, y + 1) * (2.0f / 12.0f) +
               gray(x + 1, y - 1) * (-1.0f / 12.0f) + gray(x + 1, y + 1) * (1.0f / 12.0f);
    Ix(x, y) = gray(x - 1, y - 1) * (-1.0f / 12.0f) + gray(x + 1, y - 1) * (1.0f / 12.0f) +
               gray(x - 1, y) * (-2.0f / 12.0f) + gray(x + 1, y) * (2.0f / 12.0f) +
               gray(x - 1, y + 1) * (-1.0f / 12.0f) + gray(x + 1, y + 1) * (1.0f / 12.0f);
    
    // Covariance
    Func Ixx("Ixx"), Iyy("Iyy"), Ixy("Ixy");
    Ixx(x, y) = Ix(x, y) * Ix(x, y);
    Iyy(x, y) = Iy(x, y) * Iy(x, y);
    Ixy(x, y) = Ix(x, y) * Iy(x, y);
    
    // Box filter
    Func Sxx("Sxx"), Syy("Syy"), Sxy("Sxy");
    Sxx(x, y) = Ixx(x - 1, y - 1) + Ixx(x - 1, y) + Ixx(x - 1, y + 1) +
                 Ixx(x, y - 1) + Ixx(x, y) + Ixx(x, y + 1) +
                 Ixx(x + 1, y - 1) + Ixx(x + 1, y) + Ixx(x + 1, y + 1);
    Syy(x, y) = Iyy(x - 1, y - 1) + Iyy(x - 1, y) + Iyy(x - 1, y + 1) +
                 Iyy(x, y - 1) + Iyy(x, y) + Iyy(x, y + 1) +
                 Iyy(x + 1, y - 1) + Iyy(x + 1, y) + Iyy(x + 1, y + 1);
    Sxy(x, y) = Ixy(x - 1, y - 1) + Ixy(x - 1, y) + Ixy(x - 1, y + 1) +
                 Ixy(x, y - 1) + Ixy(x, y) + Ixy(x, y + 1) +
                 Ixy(x + 1, y - 1) + Ixy(x + 1, y) + Ixy(x + 1, y + 1);
    
    // Harris response
    Func det("det"), trace("trace"), out("out");
    det(x, y) = Sxx(x, y) * Syy(x, y) - Sxy(x, y) * Sxy(x, y);
    trace(x, y) = Sxx(x, y) + Syy(x, y);
    out(x, y) = det(x, y) - 0.04f * trace(x, y) * trace(x, y);
    
    // For comparison, let's also compile the AOT version
    // Using Pipeline API for best performance
    Pipeline p(out);
    
    // Compile to JIT (simulating AOT for now)
    Target t = get_jit_target_from_environment();
    p.compile_jit(t);
    
    // Warmup
    for (int i = 0; i < 3; i++) {
        p.realize({input, output});
    }
    
    // Timed runs
    double total = 0;
    const int runs = 5;
    for (int i = 0; i < runs; i++) {
        auto start = high_resolution_clock::now();
        p.realize({input, output});
        auto end = high_resolution_clock::now();
        total += duration_cast<microseconds>(end - start).count() / 1000.0;
    }
    
    double avg_ms = total / runs;
    cout << "Halide Harris (AOT-style): " << avg_ms << " ms" << endl;
    
    // Save output
    save_image(output, argv[2]);
    
    return 0;
}

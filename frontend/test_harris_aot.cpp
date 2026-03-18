// Simple AOT Harris - basic version
#include "Halide.h"
#include <iostream>
#include <chrono>

using namespace Halide;
using namespace std;
using namespace std::chrono;

int main(int argc, char** argv) {
    const int W = 752;
    const int H = 480;
    
    Buffer<uint8_t> input(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            input(x, y) = (x + y) % 256;
        }
    }
    
    Buffer<float> output(W, H);
    
    Var x("x"), y("y");
    
    // Harris algorithm
    Func in_f("in_f");
    in_f(x, y) = BoundaryConditions::repeat_edge(input)(x, y);
    
    // Sobel
    Func Iy("Iy"), Ix("Ix");
    Iy(x, y) = in_f(x - 1, y - 1) * (-1.0f/12.0f) + in_f(x - 1, y + 1) * (1.0f/12.0f) +
               in_f(x, y - 1) * (-2.0f/12.0f) + in_f(x, y + 1) * (2.0f/12.0f) +
               in_f(x + 1, y - 1) * (-1.0f/12.0f) + in_f(x + 1, y + 1) * (1.0f/12.0f);
    Ix(x, y) = in_f(x - 1, y - 1) * (-1.0f/12.0f) + in_f(x + 1, y - 1) * (1.0f/12.0f) +
               in_f(x - 1, y) * (-2.0f/12.0f) + in_f(x + 1, y) * (2.0f/12.0f) +
               in_f(x - 1, y + 1) * (-1.0f/12.0f) + in_f(x + 1, y + 1) * (1.0f/12.0f);
    
    // Covariance
    Func Ixx("Ixx"), Iyy("Iyy"), Ixy("Ixy");
    Ixx(x, y) = Ix(x, y) * Ix(x, y);
    Iyy(x, y) = Iy(x, y) * Iy(x, y);
    Ixy(x, y) = Ix(x, y) * Iy(x, y);
    
    // Box filter
    Func Sxx("Sxx"), Syy("Syy"), Sxy("Sxy");
    Sxx(x, y) = Ixx(x-1,y-1) + Ixx(x-1,y) + Ixx(x-1,y+1) + Ixx(x,y-1) + Ixx(x,y) + Ixx(x,y+1) + Ixx(x+1,y-1) + Ixx(x+1,y) + Ixx(x+1,y+1);
    Syy(x, y) = Iyy(x-1,y-1) + Iyy(x-1,y) + Iyy(x-1,y+1) + Iyy(x,y-1) + Iyy(x,y) + Iyy(x,y+1) + Iyy(x+1,y-1) + Iyy(x+1,y) + Iyy(x+1,y+1);
    Sxy(x, y) = Ixy(x-1,y-1) + Ixy(x-1,y) + Ixy(x-1,y+1) + Ixy(x,y-1) + Ixy(x,y) + Ixy(x,y+1) + Ixy(x+1,y-1) + Ixy(x+1,y) + Ixy(x+1,y+1);
    
    // Harris response
    Func det("det"), trace("trace"), out("out");
    det(x, y) = Sxx(x, y) * Syy(x, y) - Sxy(x, y) * Sxy(x, y);
    trace(x, y) = Sxx(x, y) + Syy(x, y);
    out(x, y) = det(x, y) - 0.04f * trace(x, y) * trace(x, y);
    
    // Simple schedule - just parallelize and vectorize
    out.compute_root().parallel(y).vectorize(x, 8);
    
    // Compile and run
    Pipeline p(out);
    Target t = get_jit_target_from_environment();
    p.compile_jit(t);
    
    // Warmup
    for (int i = 0; i < 2; i++) {
        p.realize({input, output});
    }
    
    // Time it
    const int runs = 5;
    double total = 0;
    for (int i = 0; i < runs; i++) {
        auto start = high_resolution_clock::now();
        p.realize({input, output});
        auto end = high_resolution_clock::now();
        total += duration_cast<microseconds>(end - start).count() / 1000.0;
    }
    
    cout << "Harris (simple schedule): " << (total / runs) << " ms" << endl;
    
    return 0;
}

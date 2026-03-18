// Harris with better schedule
#include "Halide.h"
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
    
    Buffer<uint8_t> input(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            input(x, y) = img.at<uint8_t>(y, x);
        }
    }
    
    Buffer<float> output(W, H);
    
    Var x("x"), y("y"), yi("yi");
    
    // Harris
    Func in_f("in_f");
    in_f(x, y) = BoundaryConditions::repeat_edge(input)(x, y);
    
    Func Iy("Iy"), Ix("Ix");
    Iy(x, y) = in_f(x - 1, y - 1) * (-1.0f/12.0f) + in_f(x - 1, y + 1) * (1.0f/12.0f) +
               in_f(x, y - 1) * (-2.0f/12.0f) + in_f(x, y + 1) * (2.0f/12.0f) +
               in_f(x + 1, y - 1) * (-1.0f/12.0f) + in_f(x + 1, y + 1) * (1.0f/12.0f);
    Ix(x, y) = in_f(x - 1, y - 1) * (-1.0f/12.0f) + in_f(x + 1, y - 1) * (1.0f/12.0f) +
               in_f(x - 1, y) * (-2.0f/12.0f) + in_f(x + 1, y) * (2.0f/12.0f) +
               in_f(x - 1, y + 1) * (-1.0f/12.0f) + in_f(x + 1, y + 1) * (1.0f/12.0f);
    
    Func Ixx("Ixx"), Iyy("Iyy"), Ixy("Ixy");
    Ixx(x, y) = Ix(x, y) * Ix(x, y);
    Iyy(x, y) = Iy(x, y) * Iy(x, y);
    Ixy(x, y) = Ix(x, y) * Iy(x, y);
    
    Func Sxx("Sxx"), Syy("Syy"), Sxy("Sxy");
    Sxx(x, y) = Ixx(x-1,y-1) + Ixx(x-1,y) + Ixx(x-1,y+1) + Ixx(x,y-1) + Ixx(x,y) + Ixx(x,y+1) + Ixx(x+1,y-1) + Ixx(x+1,y) + Ixx(x+1,y+1);
    Syy(x, y) = Iyy(x-1,y-1) + Iyy(x-1,y) + Iyy(x-1,y+1) + Iyy(x,y-1) + Iyy(x,y) + Iyy(x,y+1) + Iyy(x+1,y-1) + Iyy(x+1,y) + Iyy(x+1,y+1);
    Sxy(x, y) = Ixy(x-1,y-1) + Ixy(x-1,y) + Ixy(x-1,y+1) + Ixy(x,y-1) + Ixy(x,y) + Ixy(x,y+1) + Ixy(x+1,y-1) + Ixy(x+1,y) + Ixy(x+1,y+1);
    
    Func out("out");
    out(x, y) = (Sxx(x,y)*Syy(x,y) - Sxy(x,y)*Sxy(x,y)) - 0.04f * (Sxx(x,y) + Syy(x,y)) * (Sxx(x,y) + Syy(x,y));
    
    // Better schedule - tile and compute_at
    Var y_outer, y_inner;
    out.split(y, y_outer, y_inner, 32);
    out.parallel(y_outer).vectorize(x, 8);
    
    // Compute gradients at output inner loop
    Ix.compute_at(out, y_inner).vectorize(x, 8);
    Iy.compute_at(out, y_inner).vectorize(x, 8);
    Ixx.compute_at(out, y_inner).vectorize(x, 8);
    Iyy.compute_at(out, y_inner).vectorize(x, 8);
    Ixy.compute_at(out, y_inner).vectorize(x, 8);
    
    Target t = get_jit_target_from_environment();
    cout << "Target: " << t << endl;
    out.compile_jit(t);
    cout << "Compiled" << endl;
    
    // Warmup
    out.realize(output);
    
    const int runs = 5;
    double total = 0;
    for (int i = 0; i < runs; i++) {
        auto start = high_resolution_clock::now();
        out.realize(output);
        auto end = high_resolution_clock::now();
        total += duration_cast<microseconds>(end - start).count() / 1000.0;
    }
    
    cout << "Harris: " << (total / runs) << " ms" << endl;
    
    return 0;
}

// Halide Harris AOT Test - Manual Schedule
#include "harris_manual.h"
#include <opencv2/opencv.hpp>
#include <HalideBuffer.h>
#include <iostream>
#include <chrono>

using namespace Halide::Runtime;
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
    
    // Warmup
    harris_manual(input.raw_buffer(), output.raw_buffer());
    
    // Time it
    const int runs = 5;
    double total = 0;
    for (int i = 0; i < runs; i++) {
        auto start = high_resolution_clock::now();
        harris_manual(input.raw_buffer(), output.raw_buffer());
        auto end = high_resolution_clock::now();
        total += duration_cast<microseconds>(end - start).count() / 1000.0;
    }
    
    cout << "Harris (manual AOT): " << (total / runs) << " ms" << endl;
    
    return 0;
}

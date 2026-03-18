// Halide Harris LK - AOT version
// Uses Generator-produced static library

#include "frontend/harris/harris_manual.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#include <HalideBuffer.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

static double ms_since(const Clock::time_point& start, const Clock::time_point& end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

struct HalideHarrisPipeline {
  Halide::Runtime::Buffer<uint8_t> input_buf;
  Halide::Runtime::Buffer<float> output_buf;
  int width = 0;
  int height = 0;
  
  void build(const cv::Mat& gray) {
    width = gray.cols;
    height = gray.rows;
    
    input_buf = Halide::Runtime::Buffer<uint8_t>(width, height);
    output_buf = Halide::Runtime::Buffer<float>(width, height);
    
    // Copy input data
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        input_buf(x, y) = gray.at<unsigned char>(y, x);
      }
    }
  }
  
  void run() {
    harris_manual(input_buf.raw_buffer(), output_buf.raw_buffer());
  }
};

struct OFPipeline {
  cv::Mat gray0, gray1;
  std::vector<cv::Point2f> points0, points1;
  std::vector<uchar> status;
  cv::Mat err;
  
  void build(const cv::Mat& gray) {
    gray.copyTo(gray0);
  }
  
  void run(const cv::Mat& gray, const std::vector<cv::Point2f>& pts) {
    gray.copyTo(gray1);
    cv::calcOpticalFlowPyrLK(gray0, gray1, pts, points1, status, err);
    gray1.copyTo(gray0);
  }
};

// ... rest of the file with SLAMBench benchmark code ...
// (omitted for brevity - uses the same structure as before)

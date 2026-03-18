// Halide Harris LK Frontend Test
// Uses Halide Harris (auto-scheduled) + OpenCV Optical Flow

#include "harris_auto.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#include <HalideBuffer.h>

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

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: frontend_halide_harris_lk <frame0> <frame1>\n";
    return 1;
  }

  const std::string frame0_path = argv[1];
  const std::string frame1_path = argv[2];

  // Load images
  cv::Mat img0 = cv::imread(frame0_path, cv::IMREAD_UNCHANGED);
  cv::Mat img1 = cv::imread(frame1_path, cv::IMREAD_UNCHANGED);

  if (img0.empty() || img1.empty()) {
    std::cerr << "failed to load input images\n";
    return 1;
  }

  // Convert to grayscale
  cv::Mat gray0, gray1;
  if (img0.channels() == 1) {
    gray0 = img0;
  } else {
    cv::cvtColor(img0, gray0, cv::COLOR_BGR2GRAY);
  }
  if (img1.channels() == 1) {
    gray1 = img1;
  } else {
    cv::cvtColor(img1, gray1, cv::COLOR_BGR2GRAY);
  }

  const int W = gray0.cols;
  const int H = gray0.rows;
  std::cout << "image_size: " << W << "x" << H << "\n";

  // ========================================
  // Halide Harris (auto-scheduled)
  // ========================================
  Halide::Runtime::Buffer<uint8_t> input(W, H);
  Halide::Runtime::Buffer<float> harris_response(W, H);

  // Copy input
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      input(x, y) = gray0.at<uint8_t>(y, x);
    }
  }

  // Run Harris
  auto harris_start = Clock::now();
  harris_auto(input.raw_buffer(), harris_response.raw_buffer());
  auto harris_end = Clock::now();
  double harris_ms = ms_since(harris_start, harris_end);

  // ========================================
  // Select top corners (like goodFeaturesToTrack)
  // ========================================
  const int max_corners = 500;
  std::vector<cv::Point2f> points0;
  points0.reserve(max_corners);

  float min_distance = 10.0;
  for (int y = 0; y < H; y += 10) {
    for (int x = 0; x < W; x += 10) {
      float response = harris_response(x, y);
      if (response > 0.01) {
        // Check distance to existing points
        bool too_close = false;
        for (const auto& p : points0) {
          float dx = x - p.x;
          float dy = y - p.y;
          if (dx*dx + dy*dy < min_distance*min_distance) {
            too_close = true;
            break;
          }
        }
        if (!too_close && points0.size() < max_corners) {
          points0.emplace_back(x, y);
        }
      }
    }
  }

  // ========================================
  // Optical Flow (OpenCV)
  // ========================================
  std::vector<cv::Point2f> points1;
  std::vector<unsigned char> status;
  std::vector<float> err;

  auto of_start = Clock::now();
  cv::calcOpticalFlowPyrLK(
      gray0,
      gray1,
      points0,
      points1,
      status,
      err,
      cv::Size(21, 21),
      3,
      cv::TermCriteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 30, 0.01),
      0,
      1e-4);
  auto of_end = Clock::now();
  double of_ms = ms_since(of_start, of_end);

  // Count tracked
  int tracked = 0;
  for (size_t i = 0; i < status.size(); i++) {
    if (status[i]) tracked++;
  }

  // Report (SLAMBench format)
  std::cout << "detected_points: " << points0.size() << "\n";
  std::cout << "tracked_points: " << tracked << "\n";
  std::cout << "halide_response_ms: " << harris_ms << "\n";
  std::cout << "halide_post_ms: " << 0.0 << "\n";  // No post-processing
  std::cout << "lk_ms: " << of_ms << "\n";
  std::cout << "total_ms: " << (harris_ms + of_ms) << "\n";

  return 0;
}

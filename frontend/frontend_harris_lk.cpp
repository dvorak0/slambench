#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#include <opencv2/highgui.hpp>

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
    std::cerr << "usage: frontend_harris_lk <frame0> <frame1>\n";
    return 1;
  }

  const std::string frame0_path = argv[1];
  const std::string frame1_path = argv[2];
  const std::string vis_path = "/workspace/frontend_harris_lk_vis.png";

  const auto t0 = Clock::now();
  cv::Mat img0 = cv::imread(frame0_path, cv::IMREAD_UNCHANGED);
  cv::Mat img1 = cv::imread(frame1_path, cv::IMREAD_UNCHANGED);
  const auto t1 = Clock::now();

  if (img0.empty() || img1.empty()) {
    std::cerr << "failed to load input images\n";
    return 1;
  }

  cv::Mat gray0, gray1;
  const auto t2_start = Clock::now();
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
  const auto t2_end = Clock::now();

  std::vector<cv::Point2f> points0;
  const auto t3_start = Clock::now();
  cv::goodFeaturesToTrack(
      gray0,
      points0,
      500,
      0.01,
      10.0,
      cv::Mat(),
      3,
      true,
      0.04);
  const auto t3_end = Clock::now();

  std::vector<cv::Point2f> points1;
  std::vector<unsigned char> status;
  std::vector<float> err;
  const auto t4_start = Clock::now();
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
  const auto t4_end = Clock::now();

  std::vector<cv::Point2f> tracked0;
  std::vector<cv::Point2f> tracked1;
  tracked0.reserve(points0.size());
  tracked1.reserve(points1.size());
  for (size_t i = 0; i < status.size(); ++i) {
    if (status[i]) {
      tracked0.push_back(points0[i]);
      tracked1.push_back(points1[i]);
    }
  }

  cv::Mat vis0, vis1;
  cv::cvtColor(gray0, vis0, cv::COLOR_GRAY2BGR);
  cv::cvtColor(gray1, vis1, cv::COLOR_GRAY2BGR);
  cv::Mat canvas(std::max(vis0.rows, vis1.rows), vis0.cols + vis1.cols, CV_8UC3, cv::Scalar(0, 0, 0));
  vis0.copyTo(canvas(cv::Rect(0, 0, vis0.cols, vis0.rows)));
  vis1.copyTo(canvas(cv::Rect(vis0.cols, 0, vis1.cols, vis1.rows)));

  for (size_t i = 0; i < tracked0.size(); ++i) {
    const cv::Point2f p0 = tracked0[i];
    const cv::Point2f p1 = tracked1[i] + cv::Point2f(static_cast<float>(vis0.cols), 0.0f);
    cv::circle(canvas, p0, 2, cv::Scalar(0, 255, 0), -1);
    cv::circle(canvas, p1, 2, cv::Scalar(0, 255, 255), -1);
    cv::line(canvas, p0, p1, cv::Scalar(255, 0, 0), 1);
  }

  if (!cv::imwrite(vis_path, canvas)) {
    std::cerr << "failed to save visualization to " << vis_path << "\n";
    return 1;
  }
  const auto t5 = Clock::now();

  std::cout << std::fixed << std::setprecision(3);
  std::cout << "[frontend] frame0: " << frame0_path << "\n";
  std::cout << "[frontend] frame1: " << frame1_path << "\n";
  std::cout << "[frontend] image_size: " << gray0.cols << "x" << gray0.rows << "\n";
  std::cout << "[frontend] detected_points: " << points0.size() << "\n";
  std::cout << "[frontend] tracked_points: " << tracked0.size() << "\n";
  std::cout << "[frontend] load_ms: " << ms_since(t0, t1) << "\n";
  std::cout << "[frontend] gray_ms: " << ms_since(t2_start, t2_end) << "\n";
  std::cout << "[frontend] harris_ms: " << ms_since(t3_start, t3_end) << "\n";
  std::cout << "[frontend] lk_ms: " << ms_since(t4_start, t4_end) << "\n";
  std::cout << "[frontend] total_ms: " << ms_since(t0, t5) << "\n";
  std::cout << "[frontend] saved_vis: " << vis_path << "\n";

  return 0;
}

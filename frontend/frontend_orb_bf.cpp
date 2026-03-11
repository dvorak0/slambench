#include <opencv2/features2d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

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

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: frontend_orb_bf <frame0> <frame1>\n";
    return 1;
  }

  const std::string frame0_path = argv[1];
  const std::string frame1_path = argv[2];
  const std::string vis_path = "/workspace/frontend_orb_bf_vis.png";

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

  auto orb = cv::ORB::create(500);
  std::vector<cv::KeyPoint> keypoints0;
  std::vector<cv::KeyPoint> keypoints1;
  cv::Mat descriptors0;
  cv::Mat descriptors1;

  const auto t3_start = Clock::now();
  orb->detectAndCompute(gray0, cv::noArray(), keypoints0, descriptors0);
  orb->detectAndCompute(gray1, cv::noArray(), keypoints1, descriptors1);
  const auto t3_end = Clock::now();

  std::vector<cv::DMatch> matches;
  const auto t4_start = Clock::now();
  if (!descriptors0.empty() && !descriptors1.empty()) {
    cv::BFMatcher matcher(cv::NORM_HAMMING, true);
    matcher.match(descriptors0, descriptors1, matches);
    std::sort(matches.begin(), matches.end(), [](const cv::DMatch& a, const cv::DMatch& b) {
      return a.distance < b.distance;
    });
  }
  const auto t4_end = Clock::now();

  cv::Mat vis0, vis1;
  cv::cvtColor(gray0, vis0, cv::COLOR_GRAY2BGR);
  cv::cvtColor(gray1, vis1, cv::COLOR_GRAY2BGR);
  cv::Mat canvas(std::max(vis0.rows, vis1.rows), vis0.cols + vis1.cols, CV_8UC3, cv::Scalar(0, 0, 0));
  vis0.copyTo(canvas(cv::Rect(0, 0, vis0.cols, vis0.rows)));
  vis1.copyTo(canvas(cv::Rect(vis0.cols, 0, vis1.cols, vis1.rows)));

  const size_t draw_count = std::min<size_t>(matches.size(), 100);
  for (size_t i = 0; i < draw_count; ++i) {
    const cv::DMatch& match = matches[i];
    const cv::Point2f p0 = keypoints0[match.queryIdx].pt;
    const cv::Point2f p1 = keypoints1[match.trainIdx].pt + cv::Point2f(static_cast<float>(vis0.cols), 0.0f);
    cv::circle(canvas, p0, 2, cv::Scalar(0, 255, 0), -1);
    cv::circle(canvas, p1, 2, cv::Scalar(0, 255, 255), -1);
    cv::line(canvas, p0, p1, cv::Scalar(255, 0, 0), 1);
  }

  if (!cv::imwrite(vis_path, canvas)) {
    std::cerr << "failed to save visualization to " << vis_path << "\n";
    return 1;
  }

  const double orb_ms = ms_since(t3_start, t3_end);
  const double match_ms = ms_since(t4_start, t4_end);
  const double total_ms = orb_ms + match_ms;

  std::cout << std::fixed << std::setprecision(3);
  std::cout << "[frontend] frame0: " << frame0_path << "\n";
  std::cout << "[frontend] frame1: " << frame1_path << "\n";
  std::cout << "[frontend] image_size: " << gray0.cols << "x" << gray0.rows << "\n";
  std::cout << "[frontend] keypoints0: " << keypoints0.size() << "\n";
  std::cout << "[frontend] keypoints1: " << keypoints1.size() << "\n";
  std::cout << "[frontend] descriptors0: " << descriptors0.rows << "\n";
  std::cout << "[frontend] descriptors1: " << descriptors1.rows << "\n";
  std::cout << "[frontend] matched_pairs: " << matches.size() << "\n";
  std::cout << "[frontend] load_ms: " << ms_since(t0, t1) << "\n";
  std::cout << "[frontend] gray_ms: " << ms_since(t2_start, t2_end) << "\n";
  std::cout << "[frontend] orb_ms: " << orb_ms << "\n";
  std::cout << "[frontend] match_ms: " << match_ms << "\n";
  std::cout << "[frontend] total_ms: " << total_ms << "\n";
  std::cout << "[frontend] saved_vis: " << vis_path << "\n";

  return 0;
}

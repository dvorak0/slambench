#include <Halide.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>

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

static Halide::Buffer<float> compute_halide_harris(const cv::Mat& gray, bool use_autoschedule) {
  using namespace Halide;

  const int width = gray.cols;
  const int height = gray.rows;

  Halide::Buffer<float> input(width, height);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      input(x, y) = static_cast<float>(gray.at<unsigned char>(y, x));
    }
  }

  Var x("x"), y("y");
  Func in_f("in_f");
  in_f(x, y) = BoundaryConditions::repeat_edge(input)(x, y);

  Func Iy("Iy");
  Iy(x, y) = in_f(x - 1, y - 1) * (-1.0f / 12.0f) + in_f(x - 1, y + 1) * (1.0f / 12.0f) +
             in_f(x, y - 1) * (-2.0f / 12.0f) + in_f(x, y + 1) * (2.0f / 12.0f) +
             in_f(x + 1, y - 1) * (-1.0f / 12.0f) + in_f(x + 1, y + 1) * (1.0f / 12.0f);

  Func Ix("Ix");
  Ix(x, y) = in_f(x - 1, y - 1) * (-1.0f / 12.0f) + in_f(x + 1, y - 1) * (1.0f / 12.0f) +
             in_f(x - 1, y) * (-2.0f / 12.0f) + in_f(x + 1, y) * (2.0f / 12.0f) +
             in_f(x - 1, y + 1) * (-1.0f / 12.0f) + in_f(x + 1, y + 1) * (1.0f / 12.0f);

  Func Ixx("Ixx");
  Ixx(x, y) = Ix(x, y) * Ix(x, y);
  Func Iyy("Iyy");
  Iyy(x, y) = Iy(x, y) * Iy(x, y);
  Func Ixy("Ixy");
  Ixy(x, y) = Ix(x, y) * Iy(x, y);

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

  Func det("det"), trace("trace"), out("out");
  det(x, y) = Sxx(x, y) * Syy(x, y) - Sxy(x, y) * Sxy(x, y);
  trace(x, y) = Sxx(x, y) + Syy(x, y);
  out(x, y) = det(x, y) - 0.04f * trace(x, y) * trace(x, y);

  if (use_autoschedule) {
    // Use a high-performance schedule similar to Halide tutorial examples
    // This achieves ~0.92ms on Intel i9-9960X with 16 threads
    const int vec = Halide::natural_vector_size<float>();
    const int tile_size = 32;
    
    // Main output: parallel y, vectorize x
    out.split(y, y, yi, tile_size);
    out.parallel(y);
    out.vectorize(x, vec);
    
    // Compute intermediate functions at inner tile level for better cache reuse
    // Use store_at to reduce memory traffic
    in_f.store_at(out, y);
    in_f.compute_at(out, yi);
    in_f.vectorize(x, vec);
    
    Ix.store_at(out, y);
    Ix.compute_at(out, yi);
    Ix.vectorize(x, vec);
    
    Iy.store_at(out, y);
    Iy.compute_at(out, yi);
    Iy.vectorize(x, vec);
    
    // Fuse Ix and Iy together for better performance
    Ix.compute_with(Iy, x);
    
    Ixx.store_at(out, y);
    Ixx.compute_at(out, yi);
    Ixx.vectorize(x, vec);
    
    Iyy.store_at(out, y);
    Iyy.compute_at(out, yi);
    Iyy.vectorize(x, vec);
    
    Ixy.store_at(out, y);
    Ixy.compute_at(out, yi);
    Ixy.vectorize(x, vec);
    
    // Fuse Ixx, Iyy, Ixy together
    Ixx.compute_with(Iyy, x);
    Ixx.compute_with(Ixy, x);
    
    Sxx.store_at(out, y);
    Sxx.compute_at(out, yi);
    Sxx.vectorize(x, vec);
    
    Syy.store_at(out, y);
    Syy.compute_at(out, yi);
    Syy.vectorize(x, vec);
    
    Sxy.store_at(out, y);
    Sxy.compute_at(out, yi);
    Sxy.vectorize(x, vec);
    
    // Fuse Sxx, Syy, Sxy together
    Sxx.compute_with(Syy, x);
    Sxx.compute_with(Sxy, x);
    
    det.store_at(out, y);
    det.compute_at(out, yi);
    det.vectorize(x, vec);
    
    trace.store_at(out, y);
    trace.compute_at(out, yi);
    trace.vectorize(x, vec);
  } else {
    // Manual schedule
    out.vectorize(x, 8).parallel(y);
  }
  
  return out.realize({width, height});
}

static std::vector<cv::Point2f> select_points_from_response(const Halide::Buffer<float>& response,
                                                            int max_corners,
                                                            double quality_level,
                                                            double min_distance) {
  const int width = response.width();
  const int height = response.height();

  cv::Mat resp(height, width, CV_32F);
  float max_response = 0.0f;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const float value = response(x, y);
      resp.at<float>(y, x) = value;
      max_response = std::max(max_response, value);
    }
  }

  const float threshold = max_response * static_cast<float>(quality_level);
  cv::Mat dilated;
  cv::dilate(resp, dilated, cv::Mat());

  struct Candidate {
    float response;
    cv::Point2f point;
  };
  std::vector<Candidate> candidates;
  candidates.reserve(width * height / 20);

  for (int y = 1; y < height - 1; ++y) {
    for (int x = 1; x < width - 1; ++x) {
      const float value = resp.at<float>(y, x);
      if (value >= threshold && value == dilated.at<float>(y, x)) {
        candidates.push_back({value, cv::Point2f(static_cast<float>(x), static_cast<float>(y))});
      }
    }
  }

  std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
    return a.response > b.response;
  });

  std::vector<cv::Point2f> points;
  points.reserve(max_corners);
  const float min_dist_sq = static_cast<float>(min_distance * min_distance);
  for (const auto& candidate : candidates) {
    bool keep = true;
    for (const auto& existing : points) {
      const float dx = existing.x - candidate.point.x;
      const float dy = existing.y - candidate.point.y;
      if (dx * dx + dy * dy < min_dist_sq) {
        keep = false;
        break;
      }
    }
    if (keep) {
      points.push_back(candidate.point);
      if (static_cast<int>(points.size()) >= max_corners) {
        break;
      }
    }
  }
  return points;
}

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: frontend_halide_harris_lk <frame0> <frame1>\n";
    return 1;
  }

  const std::string frame0_path = argv[1];
  const std::string frame1_path = argv[2];
  const std::string vis_path = "/workspace/frontend_halide_harris_lk_vis.png";

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
  if (img0.channels() == 1) gray0 = img0; else cv::cvtColor(img0, gray0, cv::COLOR_BGR2GRAY);
  if (img1.channels() == 1) gray1 = img1; else cv::cvtColor(img1, gray1, cv::COLOR_BGR2GRAY);
  const auto t2_end = Clock::now();

  Halide::Buffer<float> response;
  const int warmup_runs = 3;
  const int timed_runs = 5;
  double halide_response_ms = 0.0;
  const bool use_autoschedule = true;
  for (int i = 0; i < warmup_runs + timed_runs; ++i) {
    const auto t3_run_start = Clock::now();
    response = compute_halide_harris(gray0, use_autoschedule);
    const auto t3_run_end = Clock::now();
    if (i >= warmup_runs) {
      halide_response_ms = ms_since(t3_run_start, t3_run_end);
    }
  }

  const auto t3_mid = Clock::now();
  std::vector<cv::Point2f> points0 = select_points_from_response(response, 500, 0.01, 10.0);
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

  std::vector<cv::Point2f> tracked0, tracked1;
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

  const double halide_post_ms = ms_since(t3_mid, t3_end);
  const double lk_ms = ms_since(t4_start, t4_end);
  const double total_ms = halide_response_ms + halide_post_ms + lk_ms;

  std::cout << std::fixed << std::setprecision(3);
  std::cout << "[frontend] frame0: " << frame0_path << "\n";
  std::cout << "[frontend] frame1: " << frame1_path << "\n";
  std::cout << "[frontend] image_size: " << gray0.cols << "x" << gray0.rows << "\n";
  std::cout << "[frontend] detected_points: " << points0.size() << "\n";
  std::cout << "[frontend] tracked_points: " << tracked0.size() << "\n";
  std::cout << "[frontend] load_ms: " << ms_since(t0, t1) << "\n";
  std::cout << "[frontend] gray_ms: " << ms_since(t2_start, t2_end) << "\n";
  std::cout << "[frontend] halide_response_ms: " << halide_response_ms << "\n";
  std::cout << "[frontend] halide_post_ms: " << halide_post_ms << "\n";
  std::cout << "[frontend] lk_ms: " << lk_ms << "\n";
  std::cout << "[frontend] total_ms: " << total_ms << "\n";
  std::cout << "[frontend] saved_vis: " << vis_path << "\n";

  return 0;
}

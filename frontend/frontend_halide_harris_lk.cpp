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

  // Sobel-like gradients, close to OpenCV's Dx/Dy stage.
  Func dx("dx"), dy("dy");
  dx(x, y) = in_f(x - 1, y - 1) * (-1.0f / 12.0f) + in_f(x + 1, y - 1) * (1.0f / 12.0f) +
             in_f(x - 1, y) * (-2.0f / 12.0f) + in_f(x + 1, y) * (2.0f / 12.0f) +
             in_f(x - 1, y + 1) * (-1.0f / 12.0f) + in_f(x + 1, y + 1) * (1.0f / 12.0f);
  dy(x, y) = in_f(x - 1, y - 1) * (-1.0f / 12.0f) + in_f(x - 1, y + 1) * (1.0f / 12.0f) +
             in_f(x, y - 1) * (-2.0f / 12.0f) + in_f(x, y + 1) * (2.0f / 12.0f) +
             in_f(x + 1, y - 1) * (-1.0f / 12.0f) + in_f(x + 1, y + 1) * (1.0f / 12.0f);

  // OpenCV-like covariance packing stage: dx*dx, dx*dy, dy*dy.
  Func cov_xx("cov_xx"), cov_xy("cov_xy"), cov_yy("cov_yy");
  cov_xx(x, y) = dx(x, y) * dx(x, y);
  cov_xy(x, y) = dx(x, y) * dy(x, y);
  cov_yy(x, y) = dy(x, y) * dy(x, y);

  // 3x3 box filter as separable horizontal + vertical sums.
  Func cov_xx_h("cov_xx_h"), cov_xy_h("cov_xy_h"), cov_yy_h("cov_yy_h");
  cov_xx_h(x, y) = cov_xx(x - 1, y) + cov_xx(x, y) + cov_xx(x + 1, y);
  cov_xy_h(x, y) = cov_xy(x - 1, y) + cov_xy(x, y) + cov_xy(x + 1, y);
  cov_yy_h(x, y) = cov_yy(x - 1, y) + cov_yy(x, y) + cov_yy(x + 1, y);

  Func sum_xx("sum_xx"), sum_xy("sum_xy"), sum_yy("sum_yy");
  sum_xx(x, y) = cov_xx_h(x, y - 1) + cov_xx_h(x, y) + cov_xx_h(x, y + 1);
  sum_xy(x, y) = cov_xy_h(x, y - 1) + cov_xy_h(x, y) + cov_xy_h(x, y + 1);
  sum_yy(x, y) = cov_yy_h(x, y - 1) + cov_yy_h(x, y) + cov_yy_h(x, y + 1);

  Func out("out");
  Expr det = sum_xx(x, y) * sum_yy(x, y) - sum_xy(x, y) * sum_xy(x, y);
  Expr trace = sum_xx(x, y) + sum_yy(x, y);
  out(x, y) = det - 0.04f * trace * trace;

  if (use_autoschedule) {
    load_plugin("/usr/local/lib/python3.10/dist-packages/halide/lib64/libautoschedule_mullapudi2016.so");
    out.set_estimate(x, 0, width).set_estimate(y, 0, height);
    Pipeline pipeline(out);
    Target target = get_host_target();
    AutoschedulerParams params("Mullapudi2016");
    params.extra["parallelism"] = "2";
    params.extra["last_level_cache_size"] = "31457280";
    params.extra["balance"] = "40";
    pipeline.apply_autoscheduler(target, params);
  } else {
    Var yo("yo"), yi("yi");
    out.split(y, yo, yi, 32).parallel(yo).vectorize(x, 8);
    cov_xx_h.compute_at(out, yo).vectorize(x, 8);
    cov_xy_h.compute_at(out, yo).vectorize(x, 8);
    cov_yy_h.compute_at(out, yo).vectorize(x, 8);
    sum_xx.compute_at(out, yi).vectorize(x, 8);
    sum_xy.compute_at(out, yi).vectorize(x, 8);
    sum_yy.compute_at(out, yi).vectorize(x, 8);
    cov_xx.compute_at(out, yi).vectorize(x, 8);
    cov_xy.compute_at(out, yi).vectorize(x, 8);
    cov_yy.compute_at(out, yi).vectorize(x, 8);
    dx.compute_at(out, yi).vectorize(x, 8);
    dy.compute_at(out, yi).vectorize(x, 8);
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
  try {
    for (int i = 0; i < warmup_runs + timed_runs; ++i) {
      const auto t3_run_start = Clock::now();
      response = compute_halide_harris(gray0, use_autoschedule);
      const auto t3_run_end = Clock::now();
      if (i >= warmup_runs) {
        halide_response_ms = ms_since(t3_run_start, t3_run_end);
      }
    }
  } catch (const std::exception& e) {
    std::cerr << "Halide autoschedule failed: " << e.what() << "\n";
    return 1;
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

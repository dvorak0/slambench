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

// Pure C implementation mimicking OpenCV's Harris
static cv::Mat compute_harris_c(const cv::Mat& gray) {
    const int width = gray.cols;
    const int height = gray.rows;
    
    // Step 1: Sobel to get Dx, Dy as float32
    cv::Mat Dx, Dy;
    int ksize = 3;
    int blockSize = 2;
    double scale = 1.0 / (double)((1 << (ksize - 1)) * blockSize);
    
    cv::Sobel(gray, Dx, CV_32F, 1, 0, ksize, scale, 0, cv::BORDER_REPLICATE);
    cv::Sobel(gray, Dy, CV_32F, 0, 1, ksize, scale, 0, cv::BORDER_REPLICATE);
    
    // Step 2: Pack into CV_32FC3 style: [dx*dx, dx*dy, dy*dy]
    cv::Mat cov(height, width, CV_32FC3);
    for (int y = 0; y < height; y++) {
        const float* dx = Dx.ptr<float>(y);
        const float* dy = Dy.ptr<float>(y);
        cv::Vec3f* c = cov.ptr<cv::Vec3f>(y);
        for (int x = 0; x < width; x++) {
            c[x][0] = dx[x] * dx[x];  // dx*dx
            c[x][1] = dx[x] * dy[x];  // dx*dy
            c[x][2] = dy[x] * dy[x];  // dy*dy
        }
    }
    
    // Step 3: Box filter (3x3)
    cv::Mat cov_filtered;
    cv::boxFilter(cov, cov_filtered, CV_32FC3, cv::Size(3, 3), cv::Point(-1, -1), false, cv::BORDER_REPLICATE);
    
    // Step 4: Harris response: det - k * trace^2
    // det = a*c - b*b, trace = a + c, k = 0.04
    cv::Mat response(height, width, CV_32F);
    double k = 0.04;
    for (int y = 0; y < height; y++) {
        const cv::Vec3f* c = cov_filtered.ptr<cv::Vec3f>(y);
        float* r = response.ptr<float>(y);
        for (int x = 0; x < width; x++) {
            float a = c[x][0];
            float b = c[x][1];
            float c_val = c[x][2];
            r[x] = a * c_val - b * b - k * (a + c_val) * (a + c_val);
        }
    }
    
    // Return the response directly
    return response;
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

  // Sobel 3x3 - matching OpenCV
  Func Iy("Iy");
  Iy(x, y) = in_f(x - 1, y - 1) * (-1.0f) + in_f(x + 1, y - 1) * (1.0f) +
             in_f(x - 1, y) * (-2.0f) + in_f(x + 1, y) * (2.0f) +
             in_f(x - 1, y + 1) * (-1.0f) + in_f(x + 1, y + 1) * (1.0f);

  Func Ix("Ix");
  Ix(x, y) = in_f(x - 1, y - 1) * (-1.0f) + in_f(x + 1, y - 1) * (1.0f) +
             in_f(x - 1, y) * (-2.0f) + in_f(x + 1, y) * (2.0f) +
             in_f(x - 1, y + 1) * (-1.0f) + in_f(x + 1, y + 1) * (1.0f);

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
    Var yi("yi");
    const int vec = 8;
    const int tile_size = 32;
    
    out.split(y, y, yi, tile_size);
    out.parallel(y);
    out.vectorize(x, vec);
    
    in_f.compute_at(out, yi);
    Ix.compute_at(out, yi);
    Iy.compute_at(out, yi);
    Ixx.compute_at(out, yi);
    Iyy.compute_at(out, yi);
    Ixy.compute_at(out, yi);
    Sxx.compute_at(out, yi);
    Syy.compute_at(out, yi);
    Sxy.compute_at(out, yi);
    det.compute_at(out, yi);
    trace.compute_at(out, yi);
  } else {
    Var yi("yi");
    const int vec = 8;
    const int tile_size = 32;
    
    out.split(y, y, yi, tile_size);
    out.parallel(y);
    out.vectorize(x, vec);
    
    in_f.compute_at(out, yi).vectorize(x, vec);
    Ix.compute_at(out, yi).vectorize(x, vec);
    Iy.compute_at(out, yi).vectorize(x, vec);
    Ixx.compute_at(out, yi).vectorize(x, vec);
    Iyy.compute_at(out, yi).vectorize(x, vec);
    Ixy.compute_at(out, yi).vectorize(x, vec);
    Sxx.compute_at(out, yi).vectorize(x, vec);
    Syy.compute_at(out, yi).vectorize(x, vec);
    Sxy.compute_at(out, yi).vectorize(x, vec);
    det.compute_at(out, yi).vectorize(x, vec);
    trace.compute_at(out, yi).vectorize(x, vec);
  }
  
  return out.realize({width, height});
}

static std::vector<cv::Point2f> select_points_from_response(const cv::Mat& response,
                                                            int max_corners,
                                                            double quality_level,
                                                            double min_distance) {
  const int width = response.cols;
  const int height = response.rows;

  // The response is already a cv::Mat, just use it directly
  const cv::Mat& resp = response;
  float max_response = 0.0f;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const float value = resp.at<float>(y, x);
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
      const float dx = candidate.point.x - existing.x;
      const float dy = candidate.point.y - existing.y;
      if (dx * dx + dy * dy < min_dist_sq) {
        keep = false;
        break;
      }
    }
    if (keep) {
      points.emplace_back(candidate.point);
      if (static_cast<int>(points.size()) >= max_corners) break;
    }
  }

  return points;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "Usage: " << argv[0] << " <frame0.png> <frame1.png>" << std::endl;
    return 1;
  }

  std::string frame0_path = argv[1];
  std::string frame1_path = argv[2];

  cv::Mat frame0 = cv::imread(frame0_path, cv::IMREAD_COLOR);
  cv::Mat frame1 = cv::imread(frame1_path, cv::IMREAD_COLOR);

  if (frame0.empty() || frame1.empty()) {
    std::cerr << "Failed to load images" << std::endl;
    return 1;
  }

  cv::Mat gray0, gray1;
  cv::cvtColor(frame0, gray0, cv::COLOR_BGR2GRAY);
  cv::cvtColor(frame1, gray1, cv::COLOR_BGR2GRAY);

  // ==================== Pure C Harris ====================
  auto start_c = Clock::now();
  cv::Mat response_c = compute_harris_c(gray0);
  auto end_c = Clock::now();
  double c_ms = ms_since(start_c, end_c);
  
  // Warmup
  for (int i = 0; i < 3; i++) {
    cv::Mat dummy = compute_harris_c(gray0);
  }
  
  // Timed runs
  double c_total = 0;
  for (int i = 0; i < 5; i++) {
    start_c = Clock::now();
    cv::Mat response_c2 = compute_harris_c(gray0);
    end_c = Clock::now();
    c_total += ms_since(start_c, end_c);
  }
  double c_avg = c_total / 5.0;
  
  // ==================== Halide Harris ====================
  auto start = Clock::now();
  Halide::Buffer<float> response = compute_halide_harris(gray0, false);
  auto end = Clock::now();
  double halide_ms = ms_since(start, end);
  
  // Warmup
  for (int i = 0; i < 3; i++) {
    Halide::Buffer<float> dummy = compute_halide_harris(gray0, false);
  }
  
  // Timed runs
  double halide_total = 0;
  for (int i = 0; i < 5; i++) {
    start = Clock::now();
    Halide::Buffer<float> response2 = compute_halide_harris(gray0, false);
    end = Clock::now();
    halide_total += ms_since(start, end);
  }
  double halide_avg = halide_total / 5.0;

  // ==================== OpenCV Harris ====================
  cv::Mat gray0f;
  gray0.convertTo(gray0f, CV_32F, 1.0/255.0);
  
  cv::Mat harris_opencv;
  cv::cornerHarris(gray0f, harris_opencv, 2, 3, 0.04, cv::BORDER_REPLICATE);
  
  auto start_ocv = Clock::now();
  cv::cornerHarris(gray0f, harris_opencv, 2, 3, 0.04, cv::BORDER_REPLICATE);
  auto end_ocv = Clock::now();
  double ocv_ms = ms_since(start_ocv, end_ocv);
  
  // Warmup
  for (int i = 0; i < 3; i++) {
    cv::cornerHarris(gray0f, harris_opencv, 2, 3, 0.04, cv::BORDER_REPLICATE);
  }
  
  // Timed runs
  double ocv_total = 0;
  for (int i = 0; i < 5; i++) {
    start_ocv = Clock::now();
    cv::cornerHarris(gray0f, harris_opencv, 2, 3, 0.04, cv::BORDER_REPLICATE);
    end_ocv = Clock::now();
    ocv_total += ms_since(start_ocv, end_ocv);
  }
  double ocv_avg = ocv_total / 5.0;

  // ==================== Keypoint Detection ====================
  auto start_kp = Clock::now();
  std::vector<cv::Point2f> points = select_points_from_response(response_c, 500, 0.01, 10);
  auto end_kp = Clock::now();
  double kp_ms = ms_since(start_kp, end_kp);
  
  // LK Optical Flow
  std::vector<cv::Point2f> tracked_points;
  std::vector<uchar> status;
  std::vector<float> err;
  auto start_lk = Clock::now();
  cv::calcOpticalFlowPyrLK(gray0, gray1, points, tracked_points, status, err);
  auto end_lk = Clock::now();
  double lk_ms = ms_since(start_lk, end_lk);
  
  int detected = static_cast<int>(points.size());
  int tracked = 0;
  for (size_t i = 0; i < status.size(); i++) {
    if (status[i]) tracked++;
  }

  // ==================== Print Results ====================
  std::cout << "[frontend] image_size: " << gray0.cols << "x" << gray0.rows << std::endl;
  std::cout << "[frontend] detected_points: " << detected << std::endl;
  std::cout << "[frontend] tracked_points: " << tracked << std::endl;
  std::cout << "[frontend] harris_c_ms: " << c_avg << std::endl;
  std::cout << "[frontend] halide_ms: " << halide_avg << std::endl;
  std::cout << "[frontend] opencv_ms: " << ocv_avg << std::endl;
  std::cout << "[frontend] kp_ms: " << kp_ms << std::endl;
  std::cout << "[frontend] lk_ms: " << lk_ms << std::endl;
  std::cout << "[frontend] total_ms: " << (c_avg + kp_ms + lk_ms) << std::endl;

  return 0;
}

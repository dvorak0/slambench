#include "linalg.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

double dot3(const double a[3], const double b[3]) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void cross3(const double a[3], const double b[3], double out[3]) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

void mat3_mul_vec(const double R[9], const double v[3], double out[3]) {
  out[0] = R[0] * v[0] + R[3] * v[1] + R[6] * v[2];
  out[1] = R[1] * v[0] + R[4] * v[1] + R[7] * v[2];
  out[2] = R[2] * v[0] + R[5] * v[1] + R[8] * v[2];
}

void mat3_mul(const double A[9], const double B[9], double out[9]) {
  for (int c = 0; c < 3; ++c) {
    for (int r = 0; r < 3; ++r) {
      out[c * 3 + r] = A[0 * 3 + r] * B[c * 3 + 0] + A[1 * 3 + r] * B[c * 3 + 1] +
                       A[2 * 3 + r] * B[c * 3 + 2];
    }
  }
}

void mat3_identity(double I[9]) {
  memset(I, 0, sizeof(double) * 9);
  I[0] = I[4] = I[8] = 1.0;
}

void mat3_transpose(const double A[9], double At[9]) {
  At[0] = A[0];
  At[1] = A[3];
  At[2] = A[6];
  At[3] = A[1];
  At[4] = A[4];
  At[5] = A[7];
  At[6] = A[2];
  At[7] = A[5];
  At[8] = A[8];
}

static double norm3(const double v[3]) {
  return sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

void angle_axis_to_rot(const double aa[3], double R[9]) {
  double theta = norm3(aa);
  if (theta < 1e-12) {
    mat3_identity(R);
    return;
  }
  double axis[3] = {aa[0] / theta, aa[1] / theta, aa[2] / theta};
  double c = cos(theta);
  double s = sin(theta);
  double t = 1.0 - c;
  double x = axis[0], y = axis[1], z = axis[2];
  R[0] = t * x * x + c;
  R[1] = t * x * y + s * z;
  R[2] = t * x * z - s * y;
  R[3] = t * x * y - s * z;
  R[4] = t * y * y + c;
  R[5] = t * y * z + s * x;
  R[6] = t * x * z + s * y;
  R[7] = t * y * z - s * x;
  R[8] = t * z * z + c;
}

static void skew(const double v[3], double S[9]) {
  S[0] = 0;
  S[1] = -v[2];
  S[2] = v[1];
  S[3] = v[2];
  S[4] = 0;
  S[5] = -v[0];
  S[6] = -v[1];
  S[7] = v[0];
  S[8] = 0;
}

void mat3_expmap(const double w[3], double R[9]) {
  double theta = norm3(w);
  if (theta < 1e-12) {
    mat3_identity(R);
    return;
  }
  double wn[3] = {w[0] / theta, w[1] / theta, w[2] / theta};
  double K[9];
  skew(wn, K);
  double K2[9];
  mat3_mul(K, K, K2);
  double s = sin(theta);
  double c = cos(theta);
  double I[9];
  mat3_identity(I);
  for (int i = 0; i < 9; ++i) {
    R[i] = I[i] + s * K[i] + (1.0 - c) * K2[i];
  }
}

void givens(double a, double b, double *c, double *s) {
  if (fabs(b) < 1e-15) {
    *c = 1.0;
    *s = 0.0;
    return;
  }
  double r = hypot(a, b);
  *c = a / r;
  *s = -b / r;
}

void apply_givens_rows(double *M, int rows, int cols, int r1, int r2, double c, double s) {
  for (int col = 0; col < cols; ++col) {
    double x = M[col * rows + r1];
    double y = M[col * rows + r2];
    M[col * rows + r1] = c * x - s * y;
    M[col * rows + r2] = s * x + c * y;
  }
}

int qr_solve_givens(double *A, int m, int n, double *b) {
  // Column-major A (m x n), m>=n. b length m; on output, first n entries are solution.
  for (int j = 0; j < n; ++j) {
    for (int r = j + 1; r < m; ++r) {
      double a = A[j * m + j];
      double bval = A[j * m + r];
      double c, s;
      givens(a, bval, &c, &s);
      apply_givens_rows(A, m, n, j, r, c, s);
      double bj = b[j];
      double br = b[r];
      b[j] = c * bj - s * br;
      b[r] = s * bj + c * br;
    }
  }
  // Back substitution
  for (int i = n - 1; i >= 0; --i) {
    double sum = b[i];
    for (int j = i + 1; j < n; ++j) {
      sum -= A[j * m + i] * b[j];
    }
    if (fabs(A[i * m + i]) < 1e-12) return -1;
    b[i] = sum / A[i * m + i];
  }
  return 0;
}

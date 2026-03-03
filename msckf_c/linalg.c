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
  out[0] = R[0] * v[0] + R[1] * v[1] + R[2] * v[2];
  out[1] = R[3] * v[0] + R[4] * v[1] + R[5] * v[2];
  out[2] = R[6] * v[0] + R[7] * v[1] + R[8] * v[2];
}

void mat3_mul(const double A[9], const double B[9], double out[9]) {
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      out[r * 3 + c] =
          A[r * 3 + 0] * B[0 * 3 + c] + A[r * 3 + 1] * B[1 * 3 + c] + A[r * 3 + 2] * B[2 * 3 + c];
    }
  }
}

void mat3_identity(double I[9]) {
  memset(I, 0, sizeof(double) * 9);
  I[0] = I[4] = I[8] = 1.0;
}

void mat3_transpose(const double A[9], double At[9]) {
  At[0] = A[0];
  At[1] = A[1 * 3 + 0];
  At[2] = A[2 * 3 + 0];
  At[3] = A[0 * 3 + 1];
  At[4] = A[4];
  At[5] = A[2 * 3 + 1];
  At[6] = A[0 * 3 + 2];
  At[7] = A[1 * 3 + 2];
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
  R[1] = t * x * y - s * z;
  R[2] = t * x * z + s * y;
  R[3] = t * x * y + s * z;
  R[4] = t * y * y + c;
  R[5] = t * y * z - s * x;
  R[6] = t * x * z - s * y;
  R[7] = t * y * z + s * x;
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
    double x = M[r1 * cols + col];
    double y = M[r2 * cols + col];
    M[r1 * cols + col] = c * x - s * y;
    M[r2 * cols + col] = s * x + c * y;
  }
}

int qr_solve_givens(double *A, int m, int n, double *b) {
  // Incremental row-wise Givens QR (keeps interface unchanged).
  // A is row-major (m x n), b length m. On output b[0:n] stores solution.
  const double eps = 1e-15;
  double *R = calloc((size_t)n * n, sizeof(double));
  double *z = calloc((size_t)n, sizeof(double));
  double *w = malloc(sizeof(double) * n);
  if (!R || !z || !w) {
    free(R);
    free(z);
    free(w);
    return -1;
  }

  for (int row = 0; row < m; ++row) {
    const double *arow = &A[(size_t)row * n];
    memcpy(w, arow, sizeof(double) * n);
    double t = b[row];

    for (int i = 0; i < n; ++i) {
      double wi = w[i];
      if (fabs(wi) < eps) continue; // Skip structural zeros in sparse rows.

      double rii = R[i * n + i];
      double c, s;
      givens(rii, wi, &c, &s);

      for (int col = i; col < n; ++col) {
        double x = R[i * n + col];
        double y = w[col];
        R[i * n + col] = c * x - s * y;
        w[col] = s * x + c * y;
      }
      w[i] = 0.0;

      double zi = z[i];
      z[i] = c * zi - s * t;
      t = s * zi + c * t;
    }
  }

  // Back substitution: R x = z
  for (int i = n - 1; i >= 0; --i) {
    double sum = z[i];
    for (int j = i + 1; j < n; ++j) {
      sum -= R[i * n + j] * b[j];
    }
    double rii = R[i * n + i];
    if (fabs(rii) < 1e-12) {
      free(R);
      free(z);
      free(w);
      return -1;
    }
    b[i] = sum / rii;
  }

  free(R);
  free(z);
  free(w);
  return 0;
}

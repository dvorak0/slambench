#include "linalg.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
  double aa[3]; // angle-axis
  double t[3];  // translation
  double f;     // focal
  double k1, k2;
} Camera;

typedef struct {
  int cam;
  int point;
  double u;
  double v;
} Observation;

typedef struct {
  double p[3];
} Point;

typedef struct {
  int count;
  int *indices;
} PointObs;

static double wall_seconds(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void project(const Camera *cam, const double R[9], const double Pw[3], double *u, double *v) {
  double Xc[3];
  mat3_mul_vec(R, Pw, Xc);
  Xc[0] += cam->t[0];
  Xc[1] += cam->t[1];
  Xc[2] += cam->t[2];
  double invz = 1.0 / Xc[2];
  double xn = Xc[0] * invz;
  double yn = Xc[1] * invz;
  double r2 = xn * xn + yn * yn;
  double distortion = 1.0 + cam->k1 * r2 + cam->k2 * r2 * r2;
  *u = cam->f * distortion * xn;
  *v = cam->f * distortion * yn;
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

static void jacobians(const Camera *cam, const double R[9], const double Pw[3], double J_pose[12],
                      double J_point[6]) {
  double Xc[3];
  mat3_mul_vec(R, Pw, Xc);
  Xc[0] += cam->t[0];
  Xc[1] += cam->t[1];
  Xc[2] += cam->t[2];
  double invz = 1.0 / Xc[2];
  double xn = Xc[0] * invz;
  double yn = Xc[1] * invz;

  double Jpi[6];
  Jpi[0] = invz;
  Jpi[1] = 0.0;
  Jpi[2] = -xn * invz;
  Jpi[3] = 0.0;
  Jpi[4] = invz;
  Jpi[5] = -yn * invz;

  double scale = cam->f; // ignoring derivative of distortion for simplicity
  for (int i = 0; i < 6; ++i) Jpi[i] *= scale;

  double S[9];
  skew(Xc, S);
  // Build 3x6 [ -S | I ]
  double A[18];
  for (int c = 0; c < 3; ++c) {
    for (int r = 0; r < 3; ++r) {
      A[c * 3 + r] = -S[c * 3 + r];
    }
  }
  A[9] = 1;
  A[10] = 0;
  A[11] = 0;
  A[12] = 0;
  A[13] = 1;
  A[14] = 0;
  A[15] = 0;
  A[16] = 0;
  A[17] = 1;

  // J_pose = Jpi (2x3) * A (3x6) => 2x6 row-major
  for (int r = 0; r < 2; ++r) {
    for (int c = 0; c < 6; ++c) {
      double s = 0.0;
      for (int k = 0; k < 3; ++k) {
        s += Jpi[r * 3 + k] * A[c * 3 + k];
      }
      J_pose[r * 6 + c] = s;
    }
  }

  // J_point = Jpi * R  (2x3)
  for (int r = 0; r < 2; ++r) {
    for (int c = 0; c < 3; ++c) {
      double s = 0.0;
      for (int k = 0; k < 3; ++k) {
        s += Jpi[r * 3 + k] * R[c * 3 + k];
      }
      J_point[r * 3 + c] = s;
    }
  }
}

static int load_bal(const char *path, Camera **cams_out, Point **pts_out, Observation **obs_out,
                    int *nc_out, int *np_out, int *no_out) {
  FILE *f = fopen(path, "r");
  if (!f) {
    perror("open dataset");
    return -1;
  }
  int nc, np, no;
  if (fscanf(f, "%d %d %d", &nc, &np, &no) != 3) {
    fclose(f);
    return -1;
  }
  Observation *obs = malloc(sizeof(Observation) * no);
  for (int i = 0; i < no; ++i) {
    if (fscanf(f, "%d %d %lf %lf", &obs[i].cam, &obs[i].point, &obs[i].u, &obs[i].v) != 4) {
      fclose(f);
      return -1;
    }
  }
  Camera *cams = malloc(sizeof(Camera) * nc);
  for (int i = 0; i < nc; ++i) {
    Camera *c = &cams[i];
    if (fscanf(f, "%lf %lf %lf %lf %lf %lf %lf %lf %lf", &c->aa[0], &c->aa[1], &c->aa[2], &c->t[0],
               &c->t[1], &c->t[2], &c->f, &c->k1, &c->k2) != 9) {
      fclose(f);
      return -1;
    }
  }
  Point *pts = malloc(sizeof(Point) * np);
  for (int i = 0; i < np; ++i) {
    if (fscanf(f, "%lf %lf %lf", &pts[i].p[0], &pts[i].p[1], &pts[i].p[2]) != 3) {
      fclose(f);
      return -1;
    }
  }
  fclose(f);
  *cams_out = cams;
  *pts_out = pts;
  *obs_out = obs;
  *nc_out = nc;
  *np_out = np;
  *no_out = no;
  return 0;
}

static void update_poses(Camera *cams, double *R_all, const double *delta, int ncams) {
  for (int i = 0; i < ncams; ++i) {
    const double *d = &delta[i * 6];
    double dtheta[3] = {d[0], d[1], d[2]};
    double dt[3] = {d[3], d[4], d[5]};
    double dR[9];
    mat3_expmap(dtheta, dR);
    double newR[9];
    mat3_mul(dR, &R_all[i * 9], newR);
    memcpy(&R_all[i * 9], newR, sizeof(double) * 9);
    cams[i].t[0] += dt[0];
    cams[i].t[1] += dt[1];
    cams[i].t[2] += dt[2];
  }
}

int main(int argc, char **argv) {
  const char *path = (argc > 1) ? argv[1] : "data/dubrovnik/problem-16-22106-pre.txt";
  Camera *cams = NULL;
  Point *pts = NULL;
  Observation *obs = NULL;
  int nc = 0, np = 0, no = 0;
  if (load_bal(path, &cams, &pts, &obs, &nc, &np, &no) != 0) {
    fprintf(stderr, "failed to load dataset %s\n", path);
    return 1;
  }

  double *R_all = malloc(sizeof(double) * 9 * nc);
  for (int i = 0; i < nc; ++i) {
    angle_axis_to_rot(cams[i].aa, &R_all[i * 9]);
  }

  int state_dim = 6 * nc;
  double total_start = wall_seconds();
  int max_iters = 5;
  int valid_points = 0;

  // Build point -> observation index lists
  PointObs *point_obs = calloc(np, sizeof(PointObs));
  for (int i = 0; i < no; ++i) {
    point_obs[obs[i].point].count++;
  }
  for (int i = 0; i < np; ++i) {
    if (point_obs[i].count > 0) {
      point_obs[i].indices = malloc(sizeof(int) * point_obs[i].count);
      point_obs[i].count = 0;
    }
  }
  for (int i = 0; i < no; ++i) {
    int pid = obs[i].point;
    int idx = point_obs[pid].count++;
    point_obs[pid].indices[idx] = i;
  }

  for (int iter = 0; iter < max_iters; ++iter) {
    // Count rows after eliminating points: sum(max(0, 2*obs-3))
    int total_rows = 0;
    for (int pid = 0; pid < np; ++pid) {
      int c = point_obs[pid].count;
      if (c >= 2) total_rows += (2 * c - 3);
    }
    double *Hred = calloc((size_t)total_rows * state_dim, sizeof(double)); // column-major
    double *bred = calloc((size_t)total_rows, sizeof(double));
    int row_cursor = 0;
    valid_points = 0;

    for (int pid = 0; pid < np; ++pid) {
      int cnt = point_obs[pid].count;
      if (cnt < 2) continue;
      valid_points++;
      int m = 2 * cnt;
      double *Hf = calloc((size_t)m * 3, sizeof(double));
      double *Hx = calloc((size_t)m * state_dim, sizeof(double));
      double *r = calloc((size_t)m, sizeof(double));
      for (int j = 0; j < cnt; ++j) {
        const Observation *o = &obs[point_obs[pid].indices[j]];
        const Camera *c = &cams[o->cam];
        const double *R = &R_all[o->cam * 9];
        double up, vp;
        project(c, R, pts[o->point].p, &up, &vp);
        r[2 * j + 0] = up - o->u;
        r[2 * j + 1] = vp - o->v;
        double Jp[12], Jf[6];
        jacobians(c, R, pts[o->point].p, Jp, Jf);
        for (int col = 0; col < 3; ++col) {
          Hf[(2 * j + 0) + col * m] = Jf[0 * 3 + col];
          Hf[(2 * j + 1) + col * m] = Jf[1 * 3 + col];
        }
        int off = o->cam * 6;
        for (int col = 0; col < 6; ++col) {
          Hx[(2 * j + 0) + (off + col) * m] = Jp[0 * 6 + col];
          Hx[(2 * j + 1) + (off + col) * m] = Jp[1 * 6 + col];
        }
      }
      // Eliminate point via Givens on Hf columns
      for (int col = 0; col < 3; ++col) {
        for (int row = m - 1; row > col; --row) {
          double a = Hf[col * m + col];
          double b = Hf[col * m + row];
          double c_g, s_g;
          givens(a, b, &c_g, &s_g);
          apply_givens_rows(Hf, m, 3, col, row, c_g, s_g);
          apply_givens_rows(Hx, m, state_dim, col, row, c_g, s_g);
          apply_givens_vec(r, col, row, c_g, s_g);
        }
      }
      int keep_rows = m - 3;
      for (int rr = 0; rr < keep_rows; ++rr) {
        int dest_row = row_cursor + rr;
        for (int c = 0; c < state_dim; ++c) {
          Hred[c * total_rows + dest_row] = Hx[(3 + rr) + c * m];
        }
        bred[dest_row] = r[3 + rr];
      }
      row_cursor += keep_rows;
      free(Hf);
      free(Hx);
      free(r);
    }

    if (row_cursor < state_dim) {
      fprintf(stderr, "not enough constraints\n");
      free(Hred);
      free(bred);
      break;
    }

    if (qr_solve_givens(Hred, row_cursor, state_dim, bred) != 0) {
      fprintf(stderr, "qr solve failed at iter %d\n", iter);
      free(Hred);
      free(bred);
      break;
    }
    update_poses(cams, R_all, bred, nc);
    free(Hred);
    free(bred);
  }
  double total_time = wall_seconds() - total_start;

  printf("points_used: %d\n", valid_points);
  printf("iterations: %d\n", max_iters);
  printf("TIME %.3f\n", total_time);

  for (int i = 0; i < np; ++i) free(point_obs[i].indices);
  free(point_obs);
  free(R_all);
  free(cams);
  free(pts);
  free(obs);
  return 0;
}

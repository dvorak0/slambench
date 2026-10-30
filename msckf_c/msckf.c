#include "linalg.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(MSCKF_USE_LAPACK)
extern void dgels_(char *trans, int *m, int *n, int *nrhs, double *a, int *lda, double *b, int *ldb,
                   double *work, int *lwork, int *info);
#endif

#if defined(MSCKF_STORAGE_COL_MAJOR)
#if defined(MSCKF_USE_COL_QR)
#define MSCKF_GLOBAL_QR_SOLVER qr_solve_givens_cm_col_order
#else
#define MSCKF_GLOBAL_QR_SOLVER qr_solve_givens_cm_row_order
#endif
#else
#if defined(MSCKF_USE_COL_QR)
#define MSCKF_GLOBAL_QR_SOLVER qr_solve_givens_rm_col_order
#else
#define MSCKF_GLOBAL_QR_SOLVER qr_solve_givens_rm_row_order
#endif
#endif

// Keep point-level tiny solves on row-major storage.
#define MSCKF_POINT_QR_SOLVER qr_solve_givens_row_order

#if defined(MSCKF_USE_LAPACK)
static int solve_global_lapack_dgels(double *A, int m, int n, double *b) {
  int nrhs = 1;
  int lda = m;
  int ldb = m > n ? m : n;
  int info = 0;
  int lwork = -1;
  double wkopt = 0.0;
  char trans = 'N';
  dgels_(&trans, &m, &n, &nrhs, A, &lda, b, &ldb, &wkopt, &lwork, &info);
  if (info != 0) return -1;
  lwork = (int)wkopt;
  if (lwork < 1) lwork = 1;
  double *work = malloc((size_t)lwork * sizeof(double));
  if (!work) return -1;
  dgels_(&trans, &m, &n, &nrhs, A, &lda, b, &ldb, work, &lwork, &info);
  free(work);
  return info == 0 ? 0 : -1;
}
#endif

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
  // Bundler/Ceres convention uses negative z forward
  double xn = -Xc[0] * invz;
  double yn = -Xc[1] * invz;
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

static void jacobians(const Camera *cam, const double R[9], const double Pw[3], double J_cam[18],
                      double J_point[6]) {
  double Xc[3];
  double RPw[3];
  mat3_mul_vec(R, Pw, RPw);
  Xc[0] = RPw[0];
  Xc[1] = RPw[1];
  Xc[2] = RPw[2];
  Xc[0] += cam->t[0];
  Xc[1] += cam->t[1];
  Xc[2] += cam->t[2];
  double invz = 1.0 / Xc[2];
  double xn = -Xc[0] * invz;
  double yn = -Xc[1] * invz;

  double r2 = xn * xn + yn * yn;
  double g = 1.0 + cam->k1 * r2 + cam->k2 * r2 * r2;
  double dg_dxn = cam->k1 * 2.0 * xn + cam->k2 * 4.0 * r2 * xn;
  double dg_dyn = cam->k1 * 2.0 * yn + cam->k2 * 4.0 * r2 * yn;

  double f = cam->f;

  double dpx_dxn = f * (g + xn * dg_dxn);
  double dpx_dyn = f * (xn * dg_dyn);
  double dpy_dxn = f * (yn * dg_dxn);
  double dpy_dyn = f * (g + yn * dg_dyn);

  double d_xn_dX = -invz;
  double d_xn_dZ = Xc[0] * invz * invz;
  double d_yn_dY = -invz;
  double d_yn_dZ = Xc[1] * invz * invz;

  double Jpi[6];
  Jpi[0] = dpx_dxn * d_xn_dX;           // dpx/dX
  Jpi[1] = dpx_dyn * d_yn_dY;           // dpx/dY
  Jpi[2] = dpx_dxn * d_xn_dZ + dpx_dyn * d_yn_dZ; // dpx/dZ
  Jpi[3] = dpy_dxn * d_xn_dX;           // dpy/dX
  Jpi[4] = dpy_dyn * d_yn_dY;           // dpy/dY
  Jpi[5] = dpy_dxn * d_xn_dZ + dpy_dyn * d_yn_dZ; // dpy/dZ

  double S[9];
  // Rotation Jacobian is with respect to R*Pw (not translated point).
  skew(RPw, S);
  // Build 3x6 [ -S | I ]
  double A[18];
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      A[r * 6 + c] = -S[r * 3 + c];
    }
  }
  A[0 * 6 + 3] = 1;
  A[0 * 6 + 4] = 0;
  A[0 * 6 + 5] = 0;
  A[1 * 6 + 3] = 0;
  A[1 * 6 + 4] = 1;
  A[1 * 6 + 5] = 0;
  A[2 * 6 + 3] = 0;
  A[2 * 6 + 4] = 0;
  A[2 * 6 + 5] = 1;

  // Pose block (2x6): Jpi (2x3) * A (3x6)
  for (int r = 0; r < 2; ++r) {
    for (int c = 0; c < 6; ++c) {
      double s = 0.0;
      for (int k = 0; k < 3; ++k) {
        s += Jpi[r * 3 + k] * A[k * 6 + c];
      }
      J_cam[r * 9 + c] = s;
    }
  }

  // Intrinsics block (2x3): [f, k1, k2]
  J_cam[0 * 9 + 6] = g * xn;
  J_cam[1 * 9 + 6] = g * yn;
  J_cam[0 * 9 + 7] = f * xn * r2;
  J_cam[1 * 9 + 7] = f * yn * r2;
  J_cam[0 * 9 + 8] = f * xn * r2 * r2;
  J_cam[1 * 9 + 8] = f * yn * r2 * r2;

  // J_point = Jpi * R  (2x3)
  for (int r = 0; r < 2; ++r) {
    for (int c = 0; c < 3; ++c) {
      double s = 0.0;
      for (int k = 0; k < 3; ++k) {
        s += Jpi[r * 3 + k] * R[k * 3 + c];
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

static double huber_weight(double r2, double delta) {
  double r = sqrt(r2);
  if (delta <= 0.0 || r <= delta) return 1.0;
  return delta / r;
}

static double huber_cost(double r2, double delta) {
  double r = sqrt(r2);
  if (delta <= 0.0 || r <= delta) return 0.5 * r2;
  return delta * (r - 0.5 * delta);
}

static double compute_loss(const Camera *cams, const double *R_all, const Point *pts,
                           const Observation *obs, int no, double huber_delta) {
  double total = 0.0;
  for (int i = 0; i < no; ++i) {
    const Observation *o = &obs[i];
    const Camera *c = &cams[o->cam];
    const double *R = &R_all[o->cam * 9];
    double up, vp;
    project(c, R, pts[o->point].p, &up, &vp);
    double du = up - o->u;
    double dv = vp - o->v;
    total += huber_cost(du * du + dv * dv, huber_delta);
  }
  return total;
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

  int state_dim = 9 * nc;
  const double anchor_lambda = 1e-3; // gauge fixing for first camera
  const double reg_lambda = 1e-6;    // light damping for all states
  const double huber_delta = 0.0;    // robust threshold (0 = disabled to match ceres default)
  double total_start = wall_seconds();
  double residual_eval_time = 0.0;
  double jacobian_eval_time = 0.0;
  double point_elim_time = 0.0;
  double point_backsolve_time = 0.0;
  double global_camera_solve_time = 0.0;
  int residual_eval_calls = 0;
  int jacobian_eval_calls = 0;
  int point_elim_calls = 0;
  int point_backsolve_calls = 0;
  int global_camera_solve_calls = 0;
  int max_iters = 6;
  int valid_points = 0;
#if defined(MSCKF_USE_SCHUR)
  const char *global_storage_mode = "schur-dense";
  const char *global_elim_mode = "analytic-schur";
  const char *global_solver_mode = "schur_cholesky";
#elif defined(MSCKF_STORAGE_COL_MAJOR)
  const char *global_storage_mode = "col-major";
#else
  const char *global_storage_mode = "row-major";
#endif
#if !defined(MSCKF_USE_SCHUR)
#if defined(MSCKF_USE_COL_QR)
  const char *global_elim_mode = "col-order";
#else
  const char *global_elim_mode = "row-order";
#endif
#if defined(MSCKF_USE_LAPACK)
#if defined(MSCKF_USE_OPENBLAS)
  const char *global_solver_mode = "openblas_dgels";
#else
  const char *global_solver_mode = "lapack_dgels";
#endif
#else
  const char *global_solver_mode = "givens";
#endif
#endif

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

#if defined(MSCKF_USE_SCHUR)
  // Analytic Schur: after eliminating each point, accumulate camera Hessian
  // directly into global H:  H += U - W V^{-1} W^T,  b += b_c - W V^{-1} b_p
  const int max_local_dim = 9 * nc;
  int *uniq_cams = malloc(sizeof(int) * (size_t)nc);
  int *cam_local = malloc(sizeof(int) * (size_t)nc);
  double *H = malloc(sizeof(double) * (size_t)state_dim * (size_t)state_dim);
  double *dx = malloc(sizeof(double) * (size_t)state_dim);
  // Per-camera blocks: W 9x3, U 9x9, b 9 — packed, then scatter into global H after elim.
  double *W_cams = malloc(sizeof(double) * (size_t)nc * 27);
  double *WV_cams = malloc(sizeof(double) * (size_t)nc * 27);
  double *U_cams = malloc(sizeof(double) * (size_t)nc * 81);
  double *b_cams = malloc(sizeof(double) * (size_t)nc * 9);
  double *delta_pts = calloc((size_t)np * 3, sizeof(double));
  unsigned char *cache_valid = calloc((size_t)np, 1);
  int *cache_n_cams = calloc((size_t)np, sizeof(int));
  int *cache_cam_ids = malloc(sizeof(int) * (size_t)np * (size_t)nc);
  double *cache_Vinv = malloc(sizeof(double) * (size_t)np * 9);
  double *cache_bp = malloc(sizeof(double) * (size_t)np * 3);
  double *cache_W = malloc(sizeof(double) * (size_t)np * (size_t)nc * 27);
  double *R_tmp = malloc(sizeof(double) * 9 * nc);
  double *t_tmp = malloc(sizeof(double) * 3 * nc);
  Camera *cams_tmp = malloc(sizeof(Camera) * nc);
  Point *pts_tmp = malloc(sizeof(Point) * np);
  (void)max_local_dim;
#endif

  for (int iter = 0; iter < max_iters; ++iter) {
    double t_res0 = wall_seconds();
    double loss_prev = compute_loss(cams, R_all, pts, obs, no, huber_delta);
    residual_eval_time += wall_seconds() - t_res0;
    residual_eval_calls++;
    printf("iter %d loss %.6f\n", iter, loss_prev);

#if defined(MSCKF_USE_SCHUR)
    double *delta_cam = NULL;
    valid_points = 0;
    memset(H, 0, sizeof(double) * (size_t)state_dim * (size_t)state_dim);
    memset(dx, 0, sizeof(double) * (size_t)state_dim);
    memset(delta_pts, 0, sizeof(double) * (size_t)np * 3);
    memset(cache_valid, 0, (size_t)np);

    double t_jac0 = wall_seconds();
    double t_elim_acc = 0.0;
    for (int pid = 0; pid < np; ++pid) {
      int cnt = point_obs[pid].count;
      if (cnt < 2) continue;

      for (int i = 0; i < nc; ++i) cam_local[i] = -1;
      int n_cams = 0;
      for (int j = 0; j < cnt; ++j) {
        int cam = obs[point_obs[pid].indices[j]].cam;
        if (cam_local[cam] < 0) {
          cam_local[cam] = n_cams;
          uniq_cams[n_cams++] = cam;
        }
      }
      memset(W_cams, 0, sizeof(double) * (size_t)n_cams * 27);
      memset(U_cams, 0, sizeof(double) * (size_t)n_cams * 81);
      memset(b_cams, 0, sizeof(double) * (size_t)n_cams * 9);

      double V[9] = {0};
      double bp[3] = {0};

      for (int j = 0; j < cnt; ++j) {
        const Observation *o = &obs[point_obs[pid].indices[j]];
        const Camera *c = &cams[o->cam];
        const double *R = &R_all[o->cam * 9];
        double up, vp;
        project(c, R, pts[o->point].p, &up, &vp);
        double du = up - o->u;
        double dv = vp - o->v;
        double sw = 1.0;
        if (huber_delta > 0.0) {
          sw = sqrt(huber_weight(du * du + dv * dv, huber_delta));
        }
        double r0 = -sw * du;
        double r1 = -sw * dv;

        double Jc[18], Jf[6];
        jacobians(c, R, pts[o->point].p, Jc, Jf);
        double Jf0[3] = {sw * Jf[0], sw * Jf[1], sw * Jf[2]};
        double Jf1[3] = {sw * Jf[3], sw * Jf[4], sw * Jf[5]};
        double Jc0[9], Jc1[9];
        for (int col = 0; col < 9; ++col) {
          Jc0[col] = sw * Jc[col];
          Jc1[col] = sw * Jc[9 + col];
        }

        bp[0] += Jf0[0] * r0 + Jf1[0] * r1;
        bp[1] += Jf0[1] * r0 + Jf1[1] * r1;
        bp[2] += Jf0[2] * r0 + Jf1[2] * r1;
        for (int a = 0; a < 3; ++a) {
          for (int bb = a; bb < 3; ++bb) {
            double s = Jf0[a] * Jf0[bb] + Jf1[a] * Jf1[bb];
            V[a * 3 + bb] += s;
            if (a != bb) V[bb * 3 + a] += s;
          }
        }

        const int li = cam_local[o->cam];
        double *Wblk = &W_cams[(size_t)li * 27];
        double *Ublk = &U_cams[(size_t)li * 81];
        double *bblk = &b_cams[(size_t)li * 9];
        for (int row = 0; row < 9; ++row) {
          bblk[row] += Jc0[row] * r0 + Jc1[row] * r1;
          Wblk[row * 3 + 0] += Jc0[row] * Jf0[0] + Jc1[row] * Jf1[0];
          Wblk[row * 3 + 1] += Jc0[row] * Jf0[1] + Jc1[row] * Jf1[1];
          Wblk[row * 3 + 2] += Jc0[row] * Jf0[2] + Jc1[row] * Jf1[2];
          for (int col = 0; col < 9; ++col) {
            Ublk[row * 9 + col] += Jc0[row] * Jc0[col] + Jc1[row] * Jc1[col];
          }
        }
      }

      double vtrace = V[0] + V[4] + V[8];
      double veps = 1e-10 * (vtrace > 0.0 ? vtrace / 3.0 : 1.0);
      V[0] += veps;
      V[4] += veps;
      V[8] += veps;
      double Vinv[9];
      if (invert_sym3(V, Vinv) != 0) {
        continue;
      }
      valid_points++;

      double t_e0 = wall_seconds();
      // After point elim: scatter H += U - W Vinv W^T, b += b_c - W Vinv bp
      // straight into the global camera Hessian (no tall Hx / no H_local).
      for (int ci = 0; ci < n_cams; ++ci) {
        const double *Wi = &W_cams[(size_t)ci * 27];
        double *Yi = &WV_cams[(size_t)ci * 27];
        for (int r = 0; r < 9; ++r) {
          Yi[r * 3 + 0] = Wi[r * 3 + 0] * Vinv[0] + Wi[r * 3 + 1] * Vinv[3] + Wi[r * 3 + 2] * Vinv[6];
          Yi[r * 3 + 1] = Wi[r * 3 + 0] * Vinv[1] + Wi[r * 3 + 1] * Vinv[4] + Wi[r * 3 + 2] * Vinv[7];
          Yi[r * 3 + 2] = Wi[r * 3 + 0] * Vinv[2] + Wi[r * 3 + 1] * Vinv[5] + Wi[r * 3 + 2] * Vinv[8];
        }
      }
      for (int ci = 0; ci < n_cams; ++ci) {
        const double *Yi = &WV_cams[(size_t)ci * 27];
        const double *Ui = &U_cams[(size_t)ci * 81];
        const double *bi = &b_cams[(size_t)ci * 9];
        const int gi0 = uniq_cams[ci] * 9;
        for (int r = 0; r < 9; ++r) {
          dx[gi0 + r] += bi[r] - (Yi[r * 3 + 0] * bp[0] + Yi[r * 3 + 1] * bp[1] + Yi[r * 3 + 2] * bp[2]);
        }
        // Diagonal camera block: U - Y W_i^T
        {
          const double *Wi = &W_cams[(size_t)ci * 27];
          for (int r = 0; r < 9; ++r) {
            double *Hrow = &H[(gi0 + r) * state_dim + gi0];
            const double y0 = Yi[r * 3 + 0], y1 = Yi[r * 3 + 1], y2 = Yi[r * 3 + 2];
            for (int c = 0; c < 9; ++c) {
              Hrow[c] += Ui[r * 9 + c] - (y0 * Wi[c * 3 + 0] + y1 * Wi[c * 3 + 1] + y2 * Wi[c * 3 + 2]);
            }
          }
        }
        // Off-diagonal camera blocks: - Y W_j^T
        for (int cj = 0; cj < n_cams; ++cj) {
          if (cj == ci) continue;
          const double *Wj = &W_cams[(size_t)cj * 27];
          const int gj0 = uniq_cams[cj] * 9;
          for (int r = 0; r < 9; ++r) {
            double *Hrow = &H[(gi0 + r) * state_dim + gj0];
            const double y0 = Yi[r * 3 + 0], y1 = Yi[r * 3 + 1], y2 = Yi[r * 3 + 2];
            for (int c = 0; c < 9; ++c) {
              Hrow[c] -= y0 * Wj[c * 3 + 0] + y1 * Wj[c * 3 + 1] + y2 * Wj[c * 3 + 2];
            }
          }
        }
      }
      t_elim_acc += wall_seconds() - t_e0;

      cache_valid[pid] = 1;
      cache_n_cams[pid] = n_cams;
      memcpy(&cache_cam_ids[(size_t)pid * (size_t)nc], uniq_cams, sizeof(int) * (size_t)n_cams);
      memcpy(&cache_Vinv[(size_t)pid * 9], Vinv, sizeof(double) * 9);
      memcpy(&cache_bp[(size_t)pid * 3], bp, sizeof(double) * 3);
      memcpy(&cache_W[(size_t)pid * (size_t)nc * 27], W_cams, sizeof(double) * (size_t)n_cams * 27);
    }
    jacobian_eval_time += wall_seconds() - t_jac0 - t_elim_acc;
    point_elim_time += t_elim_acc;
    jacobian_eval_calls += valid_points;
    point_elim_calls += valid_points;

    for (int j = 0; j < state_dim; ++j) {
      double lam = (j < 6) ? anchor_lambda : reg_lambda;
      H[j * state_dim + j] += lam;
    }

    double t_cam_solve0 = wall_seconds();
    int solve_status = cholesky_solve(H, state_dim, dx);
    global_camera_solve_time += wall_seconds() - t_cam_solve0;
    global_camera_solve_calls++;
    if (solve_status != 0) {
      fprintf(stderr, "schur cholesky failed at iter %d\n", iter);
      break;
    }
    delta_cam = dx;

    double t_backsolve0 = wall_seconds();
    for (int pid = 0; pid < np; ++pid) {
      if (!cache_valid[pid]) continue;
      int n_cams = cache_n_cams[pid];
      const int *cams_ids = &cache_cam_ids[(size_t)pid * (size_t)nc];
      const double *Wp = &cache_W[(size_t)pid * (size_t)nc * 27];
      const double *Vinv = &cache_Vinv[(size_t)pid * 9];
      const double *bp = &cache_bp[(size_t)pid * 3];
      double WTdx[3] = {0, 0, 0};
      for (int ci = 0; ci < n_cams; ++ci) {
        const double *Wi = &Wp[(size_t)ci * 27];
        const int gi0 = cams_ids[ci] * 9;
        for (int r = 0; r < 9; ++r) {
          double d = delta_cam[gi0 + r];
          WTdx[0] += Wi[r * 3 + 0] * d;
          WTdx[1] += Wi[r * 3 + 1] * d;
          WTdx[2] += Wi[r * 3 + 2] * d;
        }
      }
      double rhs[3] = {bp[0] - WTdx[0], bp[1] - WTdx[1], bp[2] - WTdx[2]};
      delta_pts[3 * pid + 0] = Vinv[0] * rhs[0] + Vinv[1] * rhs[1] + Vinv[2] * rhs[2];
      delta_pts[3 * pid + 1] = Vinv[3] * rhs[0] + Vinv[4] * rhs[1] + Vinv[5] * rhs[2];
      delta_pts[3 * pid + 2] = Vinv[6] * rhs[0] + Vinv[7] * rhs[1] + Vinv[8] * rhs[2];
    }
    point_backsolve_time += wall_seconds() - t_backsolve0;
    point_backsolve_calls += valid_points;

    // Backtracking line search on step length
    double alpha = 1.0;
    double loss_new = loss_prev;
    int accepted = 0;
    while (alpha > 1e-4) {
      memcpy(R_tmp, R_all, sizeof(double) * 9 * nc);
      memcpy(pts_tmp, pts, sizeof(Point) * np);
      for (int i = 0; i < nc; ++i) {
        cams_tmp[i] = cams[i];
        t_tmp[3 * i + 0] = cams[i].t[0];
        t_tmp[3 * i + 1] = cams[i].t[1];
        t_tmp[3 * i + 2] = cams[i].t[2];
      }
      // Apply scaled update to temp
      for (int i = 0; i < nc; ++i) {
        const double *d = &delta_cam[i * 9];
        double dtheta[3] = {alpha * d[0], alpha * d[1], alpha * d[2]};
        double dt[3] = {alpha * d[3], alpha * d[4], alpha * d[5]};
        double dR[9];
        mat3_expmap(dtheta, dR);
        double newR[9];
        mat3_mul(dR, &R_tmp[i * 9], newR);
        memcpy(&R_tmp[i * 9], newR, sizeof(double) * 9);
        t_tmp[3 * i + 0] += dt[0];
        t_tmp[3 * i + 1] += dt[1];
        t_tmp[3 * i + 2] += dt[2];
        cams_tmp[i].t[0] = t_tmp[3 * i + 0];
        cams_tmp[i].t[1] = t_tmp[3 * i + 1];
        cams_tmp[i].t[2] = t_tmp[3 * i + 2];
        cams_tmp[i].f += alpha * d[6];
        cams_tmp[i].k1 += alpha * d[7];
        cams_tmp[i].k2 += alpha * d[8];
      }
      for (int i = 0; i < np; ++i) {
        pts_tmp[i].p[0] += alpha * delta_pts[3 * i + 0];
        pts_tmp[i].p[1] += alpha * delta_pts[3 * i + 1];
        pts_tmp[i].p[2] += alpha * delta_pts[3 * i + 2];
      }
      double t_res1 = wall_seconds();
      loss_new = compute_loss(cams_tmp, R_tmp, pts_tmp, obs, no, huber_delta);
      residual_eval_time += wall_seconds() - t_res1;
      residual_eval_calls++;
      if (loss_new < loss_prev) {
        accepted = 1;
        break;
      }
      alpha *= 0.5;
    }
    if (accepted) {
      memcpy(R_all, R_tmp, sizeof(double) * 9 * nc);
      for (int i = 0; i < nc; ++i) {
        cams[i].t[0] = t_tmp[3 * i + 0];
        cams[i].t[1] = t_tmp[3 * i + 1];
        cams[i].t[2] = t_tmp[3 * i + 2];
        const double *d = &delta_cam[i * 9];
        cams[i].f += alpha * d[6];
        cams[i].k1 += alpha * d[7];
        cams[i].k2 += alpha * d[8];
      }
      memcpy(pts, pts_tmp, sizeof(Point) * np);
      printf("  step alpha=%.4f loss=%.6f\n", alpha, loss_new);
      if (fabs(loss_prev - loss_new) < 1e-6 * (1.0 + fabs(loss_prev))) {
        max_iters = iter + 1;
        accepted = 2;
      }
    } else {
      printf("  step rejected (no decrease)\n");
    }
    if (accepted == 2) break;
#else
    // Count rows after eliminating points: sum(max(0, 2*obs-3))
    int total_rows = 0;
    for (int pid = 0; pid < np; ++pid) {
      int c = point_obs[pid].count;
      if (c >= 2) total_rows += (2 * c - 3);
    }
    int rows_with_reg = total_rows + state_dim; // per-state regularization (includes anchor)
    double *Hred = calloc((size_t)rows_with_reg * state_dim, sizeof(double));
    double *bred = calloc((size_t)rows_with_reg, sizeof(double));
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
      double t_jac0 = wall_seconds();
      for (int j = 0; j < cnt; ++j) {
        const Observation *o = &obs[point_obs[pid].indices[j]];
        const Camera *c = &cams[o->cam];
        const double *R = &R_all[o->cam * 9];
        double up, vp;
        project(c, R, pts[o->point].p, &up, &vp);
        double du = up - o->u;
        double dv = vp - o->v;
        double w = huber_weight(du * du + dv * dv, huber_delta);
        double sw = sqrt(w);
        // Solve Hx * dx ~= -r so Gauss-Newton step follows -J^T r.
        r[2 * j + 0] = -sw * du;
        r[2 * j + 1] = -sw * dv;
        double Jc[18], Jf[6];
        jacobians(c, R, pts[o->point].p, Jc, Jf);
        for (int col = 0; col < 3; ++col) {
          Hf[(2 * j + 0) * 3 + col] = sw * Jf[0 * 3 + col];
          Hf[(2 * j + 1) * 3 + col] = sw * Jf[1 * 3 + col];
        }
        int off = o->cam * 9;
        for (int col = 0; col < 9; ++col) {
          Hx[(2 * j + 0) * state_dim + (off + col)] = sw * Jc[0 * 9 + col];
          Hx[(2 * j + 1) * state_dim + (off + col)] = sw * Jc[1 * 9 + col];
        }
      }
      jacobian_eval_time += wall_seconds() - t_jac0;
      jacobian_eval_calls++;

      // Eliminate point via Givens on Hf columns
      double t_elim0 = wall_seconds();
      for (int col = 0; col < 3; ++col) {
        for (int row = m - 1; row > col; --row) {
          double a = Hf[col * 3 + col];
          double b = Hf[row * 3 + col];
          double c_g, s_g;
          givens(a, b, &c_g, &s_g);
          apply_givens_rows(Hf, m, 3, col, row, c_g, s_g);
          apply_givens_rows(Hx, m, state_dim, col, row, c_g, s_g);
          apply_givens_vec(r, col, row, c_g, s_g);
        }
      }
      point_elim_time += wall_seconds() - t_elim0;
      point_elim_calls++;

      int keep_rows = m - 3;
      for (int rr = 0; rr < keep_rows; ++rr) {
        int dest_row = row_cursor + rr;
        for (int c = 0; c < state_dim; ++c) {
#if defined(MSCKF_STORAGE_COL_MAJOR)
          Hred[c * rows_with_reg + dest_row] = Hx[(3 + rr) * state_dim + c];
#else
          Hred[dest_row * state_dim + c] = Hx[(3 + rr) * state_dim + c];
#endif
        }
        bred[dest_row] = r[3 + rr];
      }
      row_cursor += keep_rows;
      free(Hf);
      free(Hx);
      free(r);
    }

    // Add per-state damping (first camera uses anchor_lambda)
    for (int j = 0; j < state_dim; ++j) {
      double lam = (j < 6) ? anchor_lambda : reg_lambda;
      int rrow = row_cursor + j;
#if defined(MSCKF_STORAGE_COL_MAJOR)
      Hred[j * rows_with_reg + rrow] = sqrt(lam);
#else
      Hred[rrow * state_dim + j] = sqrt(lam);
#endif
      bred[rrow] = 0.0;
    }
    row_cursor += state_dim;

    if (row_cursor < state_dim) {
      fprintf(stderr, "not enough constraints\n");
      free(Hred);
      free(bred);
      break;
    }

    double t_cam_solve0 = wall_seconds();
#if defined(MSCKF_USE_LAPACK)
    int qr_status = solve_global_lapack_dgels(Hred, row_cursor, state_dim, bred);
#else
    int qr_status = MSCKF_GLOBAL_QR_SOLVER(Hred, row_cursor, state_dim, bred);
#endif
    global_camera_solve_time += wall_seconds() - t_cam_solve0;
    global_camera_solve_calls++;
    if (qr_status != 0) {
      fprintf(stderr, "qr solve failed at iter %d\n", iter);
      free(Hred);
      free(bred);
      break;
    }
    // Recover point increments so the next linearization is consistent with the Schur step.
    double *delta_pts = calloc((size_t)np * 3, sizeof(double));
    for (int pid = 0; pid < np; ++pid) {
      int cnt = point_obs[pid].count;
      if (cnt < 2) continue;
      int m = 2 * cnt;
      double *Hf = calloc((size_t)m * 3, sizeof(double));
      double *rhs = calloc((size_t)m, sizeof(double));
      double t_jac1 = wall_seconds();
      for (int j = 0; j < cnt; ++j) {
        const Observation *o = &obs[point_obs[pid].indices[j]];
        const Camera *c = &cams[o->cam];
        const double *R = &R_all[o->cam * 9];
        double up, vp;
        project(c, R, pts[o->point].p, &up, &vp);
        double du = up - o->u;
        double dv = vp - o->v;
        double w = huber_weight(du * du + dv * dv, huber_delta);
        double sw = sqrt(w);
        double Jc[18], Jf[6];
        jacobians(c, R, pts[o->point].p, Jc, Jf);
        for (int col = 0; col < 3; ++col) {
          Hf[(2 * j + 0) * 3 + col] = sw * Jf[0 * 3 + col];
          Hf[(2 * j + 1) * 3 + col] = sw * Jf[1 * 3 + col];
        }
        int off = o->cam * 9;
        double hx0 = 0.0;
        double hx1 = 0.0;
        for (int col = 0; col < 9; ++col) {
          hx0 += sw * Jc[0 * 9 + col] * bred[off + col];
          hx1 += sw * Jc[1 * 9 + col] * bred[off + col];
        }
        rhs[2 * j + 0] = -sw * du - hx0;
        rhs[2 * j + 1] = -sw * dv - hx1;
      }
      jacobian_eval_time += wall_seconds() - t_jac1;
      jacobian_eval_calls++;

      double t_backsolve0 = wall_seconds();
      int qr_point_status = MSCKF_POINT_QR_SOLVER(Hf, m, 3, rhs);
      point_backsolve_time += wall_seconds() - t_backsolve0;
      point_backsolve_calls++;
      if (qr_point_status == 0) {
        delta_pts[3 * pid + 0] = rhs[0];
        delta_pts[3 * pid + 1] = rhs[1];
        delta_pts[3 * pid + 2] = rhs[2];
      }
      free(Hf);
      free(rhs);
    }

    // Backtracking line search on step length
    double *R_tmp = malloc(sizeof(double) * 9 * nc);
    double *t_tmp = malloc(sizeof(double) * 3 * nc);
    Camera *cams_tmp = malloc(sizeof(Camera) * nc);
    Point *pts_tmp = malloc(sizeof(Point) * np);
    double alpha = 1.0;
    double loss_new = loss_prev;
    int accepted = 0;
    while (alpha > 1e-4) {
      memcpy(R_tmp, R_all, sizeof(double) * 9 * nc);
      memcpy(pts_tmp, pts, sizeof(Point) * np);
      for (int i = 0; i < nc; ++i) {
        cams_tmp[i] = cams[i];
        t_tmp[3 * i + 0] = cams[i].t[0];
        t_tmp[3 * i + 1] = cams[i].t[1];
        t_tmp[3 * i + 2] = cams[i].t[2];
      }
      // Apply scaled update to temp
      for (int i = 0; i < nc; ++i) {
        const double *d = &bred[i * 9];
        double dtheta[3] = {alpha * d[0], alpha * d[1], alpha * d[2]};
        double dt[3] = {alpha * d[3], alpha * d[4], alpha * d[5]};
        double dR[9];
        mat3_expmap(dtheta, dR);
        double newR[9];
        mat3_mul(dR, &R_tmp[i * 9], newR);
        memcpy(&R_tmp[i * 9], newR, sizeof(double) * 9);
        t_tmp[3 * i + 0] += dt[0];
        t_tmp[3 * i + 1] += dt[1];
        t_tmp[3 * i + 2] += dt[2];
        cams_tmp[i].t[0] = t_tmp[3 * i + 0];
        cams_tmp[i].t[1] = t_tmp[3 * i + 1];
        cams_tmp[i].t[2] = t_tmp[3 * i + 2];
        cams_tmp[i].f += alpha * d[6];
        cams_tmp[i].k1 += alpha * d[7];
        cams_tmp[i].k2 += alpha * d[8];
      }
      for (int i = 0; i < np; ++i) {
        pts_tmp[i].p[0] += alpha * delta_pts[3 * i + 0];
        pts_tmp[i].p[1] += alpha * delta_pts[3 * i + 1];
        pts_tmp[i].p[2] += alpha * delta_pts[3 * i + 2];
      }
      double t_res1 = wall_seconds();
      loss_new = compute_loss(cams_tmp, R_tmp, pts_tmp, obs, no, huber_delta);
      residual_eval_time += wall_seconds() - t_res1;
      residual_eval_calls++;
      if (loss_new < loss_prev) {
        accepted = 1;
        break;
      }
      alpha *= 0.5;
    }
    if (accepted) {
      memcpy(R_all, R_tmp, sizeof(double) * 9 * nc);
      for (int i = 0; i < nc; ++i) {
        cams[i].t[0] = t_tmp[3 * i + 0];
        cams[i].t[1] = t_tmp[3 * i + 1];
        cams[i].t[2] = t_tmp[3 * i + 2];
        const double *d = &bred[i * 9];
        cams[i].f += alpha * d[6];
        cams[i].k1 += alpha * d[7];
        cams[i].k2 += alpha * d[8];
      }
      memcpy(pts, pts_tmp, sizeof(Point) * np);
      printf("  step alpha=%.4f loss=%.6f\n", alpha, loss_new);
    } else {
      printf("  step rejected (no decrease)\n");
    }
    free(R_tmp);
    free(t_tmp);
    free(cams_tmp);
    free(pts_tmp);
    free(delta_pts);
    free(Hred);
    free(bred);
#endif
  }
  double total_time = wall_seconds() - total_start;

#if defined(MSCKF_USE_SCHUR)
  free(uniq_cams);
  free(cam_local);
  free(H);
  free(dx);
  free(W_cams);
  free(WV_cams);
  free(U_cams);
  free(b_cams);
  free(delta_pts);
  free(cache_valid);
  free(cache_n_cams);
  free(cache_cam_ids);
  free(cache_Vinv);
  free(cache_bp);
  free(cache_W);
  free(R_tmp);
  free(t_tmp);
  free(cams_tmp);
  free(pts_tmp);
#endif

  printf("points_used: %d\n", valid_points);
  printf("dataset_points: %d\n", np);
  printf("dataset_observations: %d\n", no);
  printf("iterations: %d\n", max_iters);
  printf("global_qr_solver: %s\n", global_solver_mode);
  printf("global_qr_storage: %s\n", global_storage_mode);
  printf("global_qr_order: %s\n", global_elim_mode);
  printf("Residual only evaluation: %.6f s (%d calls)\n", residual_eval_time, residual_eval_calls);
  printf("Jacobian evaluation: %.6f s (%d calls)\n", jacobian_eval_time, jacobian_eval_calls);
  printf("Point elimination: %.6f s (%d calls)\n", point_elim_time, point_elim_calls);
  printf("Point backsolve: %.6f s (%d calls)\n", point_backsolve_time, point_backsolve_calls);
  printf("Global camera solve: %.6f s (%d calls)\n", global_camera_solve_time,
         global_camera_solve_calls);
  printf("TIME %.3f\n", total_time);

  for (int i = 0; i < np; ++i) free(point_obs[i].indices);
  free(point_obs);
  free(R_all);
  free(cams);
  free(pts);
  free(obs);
  return 0;
}

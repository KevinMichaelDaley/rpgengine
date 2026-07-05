#include "ferrum/procgen/srd/srd_eikonal.h"
#include <cmath>
#include <cstring>

double srd_eikonal_solve_2d(int nx, int nz, const double *occ,
                            int src_x, int src_z,
                            double *T_out) {
    double h = 1.0;
    double inf = 1e30;

    for (int i = 0; i < nx * nz; i++) T_out[i] = inf;
    T_out[src_z * nx + src_x] = 0.0;

    for (int sweep = 0; sweep < 8; sweep++) {
        for (int zi = 0; zi < nz; zi++) {
            for (int xi = 0; xi < nx; xi++) {
                if (xi == src_x && zi == src_z) continue;

                int idx = zi * nx + xi;
                double v = 1.0 - occ[idx];
                if (v < 1e-6) { T_out[idx] = inf; continue; }

                double best = T_out[idx];
                if (xi > 0)      best = fmin(best, T_out[idx - 1] + h / v);
                if (xi < nx - 1) best = fmin(best, T_out[idx + 1] + h / v);
                if (zi > 0)      best = fmin(best, T_out[idx - nx] + h / v);
                if (zi < nz - 1) best = fmin(best, T_out[idx + nx] + h / v);
                T_out[idx] = best;
            }
        }
    }

    return T_out[src_z * nx + src_x];
}

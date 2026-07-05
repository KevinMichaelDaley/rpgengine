#include "ferrum/procgen/srd/srd_transport.h"
#include <cmath>
#include <cstring>

double srd_transport_solve_2d(int nx, int nz, const double *occ,
                               int src_x, int src_z,
                               int tgt_x, int tgt_z,
                               double *R_out) {
    double eps   = 0.01;
    double delta = 10.0;

    double dx = (double)(tgt_x - src_x);
    double dz = (double)(tgt_z - src_z);
    double dnorm = sqrt(dx*dx + dz*dz) + 1e-12;
    double nx_dir = dx / dnorm;
    double nz_dir = dz / dnorm;

    int total = nx * nz;
    memset(R_out, 0, total * sizeof(double));
    R_out[src_z * nx + src_x] = 1.0;

    double *R_new = new double[total];
    for (int iter = 0; iter < 500; iter++) {
        memcpy(R_new, R_out, total * sizeof(double));

        for (int zi = 1; zi < nz - 1; zi++) {
            for (int xi = 1; xi < nx - 1; xi++) {
                if (xi == src_x && zi == src_z) { R_new[zi*nx+xi] = 1.0; continue; }
                if (xi == 0 || xi == nx-1 || zi == 0 || zi == nz-1) { R_new[zi*nx+xi] = 0.0; continue; }

                int idx = zi * nx + xi;
                double o = occ[idx];
                double axx = eps + (1-eps) * nx_dir * nx_dir + o * delta * nz_dir * nz_dir;
                double azz = eps + (1-eps) * nz_dir * nz_dir + o * delta * nx_dir * nx_dir;

                double sum = 0.0;
                sum += axx * R_out[idx - 1];
                sum += axx * R_out[idx + 1];
                sum += azz * R_out[idx - nx];
                sum += azz * R_out[idx + nx];
                double diag = 2.0 * (axx + azz);
                if (diag > 0.0) R_new[idx] = sum / diag;
            }
        }

        double diff = 0.0;
        for (int i = 0; i < total; i++) diff += fabs(R_new[i] - R_out[i]);
        memcpy(R_out, R_new, total * sizeof(double));
        if (diff < 1e-8) break;
    }

    delete[] R_new;
    return R_out[tgt_z * nx + tgt_x];
}

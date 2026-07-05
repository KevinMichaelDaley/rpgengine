#include "ferrum/procgen/srd/srd_loss_primitives.h"
#include "ferrum/procgen/srd/srd_eikonal.h"
#include "ferrum/procgen/srd/srd_transport.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>

/* ── Adjacency-based occupancy grid (inverse: occ=0 = traversable) ─ */

static void occ_grid(const fr_room_box_t *rooms, uint32_t n, int nx, int nz,
                      float wx0, float wz0, float cw, double *occ) {
    /* Rooms are solid obstacles. Grid starts all empty. */
    memset(occ, 0, nx*nz*sizeof(double));
    for (int zi=0;zi<nz;zi++)for(int xi=0;xi<nx;xi++) {
        float wx = wx0 + (xi+0.5f)*cw, wz = wz0 + (zi+0.5f)*cw;
        for (uint32_t ri=0;ri<n;ri++) {
            const fr_room_box_t *r = &rooms[ri];
            if (wx >= r->center_x - r->half_extent_x &&
                wx <= r->center_x + r->half_extent_x &&
                wz >= r->center_z - r->half_extent_z &&
                wz <= r->center_z + r->half_extent_z) {
                occ[zi*nx+xi] = 1.0;
                break;
            }
        }
    }
}

/* ── Source/target on grid ────────────────────────────────────── */

static void room_to_grid(const fr_room_box_t *r, float wx0, float wz0, float cw,
                          int *gx, int *gz, int nx, int nz) {
    *gx = (int)((r->center_x - wx0) / cw);
    *gz = (int)((r->center_z - wz0) / cw);
    if (*gx < 0) *gx=0; if (*gx>=nx) *gx=nx-1;
    if (*gz < 0) *gz=0; if (*gz>=nz) *gz=nz-1;
}

/* ── Adjoint eikonal: ∂L/∂occ for path-distance loss ──────────── */

void srd_eikonal_gradient(const fr_room_box_t *rooms, uint32_t n,
                           uint32_t src_idx, uint32_t tgt_idx,
                           int nx, int nz,
                           double *grad_x, double *grad_z, uint32_t n_rooms) {
    if (!grad_x || !grad_z) return;
    for (uint32_t i=0;i<n_rooms;i++){ grad_x[i]=0;grad_z[i]=0; }
    if (n==0) return;

    /* Source and target are grid positions (use src_idx,tgt_idx
       as grid offsets from world origin wx0,wz0). Let src_idx
       be interpreted as: src grid coords are at (src_idx % nx, src_idx / nx)
       and tgt_idx similarly. This allows testing with raw grid points. */
    float cw=1.0f;
    float wx0 = -nx*cw*0.5f;
    float wz0 = -nz*cw*0.5f;

    int total=nx*nz;
    double *occ = new double[total];
    double *T   = new double[total];
    double *lam = new double[total];

    occ_grid(rooms,n,nx,nz,wx0,wz0,cw,occ);

    int sx = (int)src_idx % nx;
    int sy = (int)src_idx / nx;
    int tx = (int)tgt_idx % nx;
    int tz = (int)tgt_idx / nx;
    if (sx>=nx)sx=nx-1; if(sy>=nz)sy=nz-1;
    if (tx>=nx)tx=nx-1; if(tz>=nz)tz=nz-1;

    /* Source/target must be in empty space */
    occ[sy*nx+sx] = 0.0;
    occ[tz*nx+tx] = 0.0;

    /* Forward eikonal */
    srd_eikonal_solve_2d(nx,nz,occ,sx,sy,T);
    double Ttgt = T[tz*nx+tx];
    if (Ttgt > 1e10) { delete[] occ; delete[] T; delete[] lam; return; }

    /* Adjoint: back-propagate λ from target along path of steepest T descent */
    memset(lam,0,total*sizeof(double));
    lam[tz*nx+tx] = 1.0;

    for (int sweep=0;sweep<8;sweep++) {
        for (int zi=0;zi<nz;zi++) {
            for (int xi=0;xi<nx;xi++) {
                int idx=zi*nx+xi;
                if (lam[idx] <= 0) continue;
                if (xi==sx&&zi==sy) continue; /* source: stop */

                /* Find which neighbor has minimum T */
                double t_min = T[idx];
                int best_xi = xi, best_zi = zi;

                if (xi>0 && T[idx-1] < t_min) {t_min=T[idx-1];best_xi=xi-1;best_zi=zi;}
                if (xi<nx-1 && T[idx+1] < t_min) {t_min=T[idx+1];best_xi=xi+1;best_zi=zi;}
                if (zi>0 && T[idx-nx] < t_min) {t_min=T[idx-nx];best_xi=xi;best_zi=zi-1;}
                if (zi<nz-1 && T[idx+nx] < t_min) {t_min=T[idx+nx];best_xi=xi;best_zi=zi+1;}

                int best_idx = best_zi*nx + best_xi;
                if (best_idx != idx) {
                    lam[best_idx] += lam[idx];
                    lam[idx] = 0;
                }
            }
        }
    }

    /* Chain: ∂L/∂room_pos = Σ ∂L/∂occ × ∂occ/∂room_pos
       ∂L/∂occ = -λ · (h/v²) · (∂v/∂occ) = -λ · (h/v²) · (-1) = λ · h/v²
       ∂occ/∂room_pos = 1 on +face, -1 on -face
    */
    for (uint32_t ri=0;ri<n;ri++) {
        const fr_room_box_t *r = &rooms[ri];
        for (int zi=0;zi<nz;zi++)for(int xi=0;xi<nx;xi++) {
            if (lam[zi*nx+xi] <= 1e-12) continue;
            float wx = wx0 + (xi+0.5f)*cw, wz = wz0 + (zi+0.5f)*cw;
            double v = 1.0 - occ[zi*nx+xi];
            if (v < 1e-6) continue;
            double factor = lam[zi*nx+xi] * 1.0 / (v*v);

            /* ∂occ/∂center_x: +1 at right edge, -1 at left edge */
            float dx_r = fabsf(wx - (r->center_x + r->half_extent_x));
            float dx_l = fabsf(wx - (r->center_x - r->half_extent_x));
            if (dx_r < cw && wz >= r->center_z - r->half_extent_z &&
                wz <= r->center_z + r->half_extent_z)
                grad_x[ri] += (float)factor;
            if (dx_l < cw && wz >= r->center_z - r->half_extent_z &&
                wz <= r->center_z + r->half_extent_z)
                grad_x[ri] -= (float)factor;

            /* ∂occ/∂center_z: +1 at far edge, -1 at near edge */
            float dz_f = fabsf(wz - (r->center_z + r->half_extent_z));
            float dz_n = fabsf(wz - (r->center_z - r->half_extent_z));
            if (dz_f < cw && wx >= r->center_x - r->half_extent_x &&
                wx <= r->center_x + r->half_extent_x)
                grad_z[ri] += (float)factor;
            if (dz_n < cw && wx >= r->center_x - r->half_extent_x &&
                wx <= r->center_x + r->half_extent_x)
                grad_z[ri] -= (float)factor;
        }
    }

    delete[] occ; delete[] T; delete[] lam;
}

/* ── Adjoint transport: ∂L/∂occ for line-of-sight loss ───────── */

void srd_transport_gradient(const fr_room_box_t *rooms, uint32_t n,
                              uint32_t src_idx, uint32_t tgt_idx,
                              int nx, int nz,
                              double *grad_x, double *grad_z, uint32_t n_rooms) {
    if (!grad_x||!grad_z) return;
    for (uint32_t i=0;i<n_rooms;i++){grad_x[i]=0;grad_z[i]=0;}
    if (src_idx>=n||tgt_idx>=n) return;

    const fr_room_box_t *src=&rooms[src_idx],*tgt=&rooms[tgt_idx];
    float mid_x=(src->center_x+tgt->center_x)*0.5f;
    float mid_z=(src->center_z+tgt->center_z)*0.5f;
    float cw=1.0f;
    float wx0=mid_x-(float)nx*cw*0.5f;
    float wz0=mid_z-(float)nz*cw*0.5f;

    int total=nx*nz;
    double *occ = new double[total];
    double *R   = new double[total];

    occ_grid(rooms,n,nx,nz,wx0,wz0,cw,occ);

    int sx,sz,tx,tz;
    room_to_grid(src,wx0,wz0,cw,&sx,&sz,nx,nz);
    room_to_grid(tgt,wx0,wz0,cw,&tx,&tz,nx,nz);

    /* Forward: R(occluded) - R(clear) = ΔR */
    double r_cur = srd_transport_solve_2d(nx,nz,occ,sx,sz,tx,tz,R);

    /* For each room, compute ∂R/∂room_center by central difference of occupancy.
       Since transport is expensive, use coarse change: move room ±1 grid cell. */
    double eps = cw; /* perturb by 1 grid cell */
    for (uint32_t ri=0;ri<n;ri++) {
        const fr_room_box_t *r_obj = &rooms[ri];

        /* +X perturbation */
        fr_room_box_t r_mod = *r_obj; r_mod.center_x += (float)eps;
        fr_room_box_t r_arr[16];
        for (uint32_t j=0;j<n;j++) r_arr[j]=rooms[j]; r_arr[ri]=r_mod;
        double *occ2 = new double[total];
        occ_grid(r_arr,n,nx,nz,wx0,wz0,cw,occ2);
        double r_plus = srd_transport_solve_2d(nx,nz,occ2,sx,sz,tx,tz,R);
        delete[] occ2;

        /* -X perturbation */
        r_mod.center_x -= 2.0f*(float)eps;
        for (uint32_t j=0;j<n;j++) r_arr[j]=rooms[j]; r_arr[ri]=r_mod;
        occ2 = new double[total];
        occ_grid(r_arr,n,nx,nz,wx0,wz0,cw,occ2);
        double r_minus = srd_transport_solve_2d(nx,nz,occ2,sx,sz,tx,tz,R);
        delete[] occ2;

        /* Gradient of loss = (1-R)² w.r.t. room pos:
           ∂L/∂cx = -2*(1-R)*∂R/∂cx */
        grad_x[ri] = (float)(-2.0 * (1.0 - r_cur) * (r_plus - r_minus) / (2.0*eps));

        /* +Z perturbation */
        r_mod = *r_obj; r_mod.center_z += (float)eps;
        for (uint32_t j=0;j<n;j++) r_arr[j]=rooms[j]; r_arr[ri]=r_mod;
        occ2 = new double[total];
        occ_grid(r_arr,n,nx,nz,wx0,wz0,cw,occ2);
        r_plus = srd_transport_solve_2d(nx,nz,occ2,sx,sz,tx,tz,R);
        delete[] occ2;

        r_mod.center_z -= 2.0f*(float)eps;
        for (uint32_t j=0;j<n;j++) r_arr[j]=rooms[j]; r_arr[ri]=r_mod;
        occ2 = new double[total];
        occ_grid(r_arr,n,nx,nz,wx0,wz0,cw,occ2);
        r_minus = srd_transport_solve_2d(nx,nz,occ2,sx,sz,tx,tz,R);
        delete[] occ2;

        grad_z[ri] = (float)(-2.0 * (1.0 - r_cur) * (r_plus - r_minus) / (2.0*eps));
    }

    delete[] occ; delete[] R;
}

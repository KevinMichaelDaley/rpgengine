#ifndef FERRUM_PROCGEN_SRD_TRANSPORT_H
#define FERRUM_PROCGEN_SRD_TRANSPORT_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Solve anisotropic gradient transport on a coarse 2D grid.
 *
 * ∇·(a(x)∇R) = 0 with R = 1 at source, R = 0 on boundaries.
 * Uses Jacobi iteration on the given grid.
 *
 * @param nx, nz   Grid dimensions.
 * @param occ      Occupancy array (nx*nz): 1 = solid, 0 = empty.
 * @param src_x, src_z  Source position (grid coords).
 * @param tgt_x, tgt_z  Target position (grid coords, for direction).
 * @param R_out    Output visibility field (nx*nz, caller-allocated).
 * @return R at the target position.
 */
double srd_transport_solve_2d(int nx, int nz, const double *occ,
                               int src_x, int src_z,
                               int tgt_x, int tgt_z,
                               double *R_out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROCGEN_SRD_TRANSPORT_H */

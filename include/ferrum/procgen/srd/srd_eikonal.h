#ifndef FERRUM_PROCGEN_SRD_EIKONAL_H
#define FERRUM_PROCGEN_SRD_EIKONAL_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Solve the eikonal equation on a coarse 2D grid (XZ plane).
 *
 * |∇T| = 1/v  with T = 0 at source (src_x, src_z).
 * Uses fast sweeping on the given grid.
 *
 * @param nx, nz   Grid dimensions.
 * @param occ      Occupancy array (nx*nz): 1 = solid, 0 = empty.
 * @param src_x, src_z  Source position (grid coords).
 * @param T_out    Output travel-time field (nx*nz, caller-allocated).
 * @return T at the source position (0.0).
 */
double srd_eikonal_solve_2d(int nx, int nz, const double *occ,
                            int src_x, int src_z,
                            double *T_out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROCGEN_SRD_EIKONAL_H */

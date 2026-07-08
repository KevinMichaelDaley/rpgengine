/**
 * @file srd_sdf_grid.h
 * @brief Dense 3D signed distance field grid for SRD room geometry.
 *
 * Voxel values follow the SDF convention:
 *   negative = inside (room interior / air)
 *   positive = outside (solid wall / rock)
 *   zero     = surface boundary
 *
 * The grid is axis-aligned in world space. Voxel (0,0,0) corresponds
 * to origin[]; voxel (x,y,z) maps to world position
 *   (origin[0] + x * voxel_size, origin[1] + y * voxel_size, origin[2] + z * voxel_size).
 *
 * Types (1): srd_sdf_grid_t
 */
#ifndef SRD_SDF_GRID_H
#define SRD_SDF_GRID_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Large positive value returned for out-of-bounds lookups. */
#define SRD_SDF_OUTSIDE 1e6f

/** @brief Default positive value used to initialize the grid (everything solid). */
#define SRD_SDF_INIT_VALUE 1e4f

/**
 * @brief Dense 3D signed distance field grid.
 *
 * Ownership: the values array is owned by the grid and freed by
 * srd_sdf_grid_destroy(). After destroy, the struct is zeroed.
 *
 * @note Not safe for concurrent access. Callers must synchronize.
 */
typedef struct {
    float *values;      /**< Dense SDF values, row-major [z * ny * nx + y * nx + x]. */
    int    nx;          /**< Grid dimension along X. */
    int    ny;          /**< Grid dimension along Y (up). */
    int    nz;          /**< Grid dimension along Z. */
    float  voxel_size;  /**< Meters per voxel. */
    float  origin[3];   /**< World-space position of voxel (0,0,0). */
} srd_sdf_grid_t;

/* ── Lifecycle (srd_sdf_grid.c) ────────────────────────────────── */

/**
 * @brief Initialize a grid with the given dimensions and voxel size.
 *
 * Allocates the values array and fills it with SRD_SDF_INIT_VALUE
 * (all voxels start as solid / outside).
 *
 * @param grid       Output grid. Must not be NULL.
 * @param nx,ny,nz   Dimensions (must all be > 0).
 * @param voxel_size Meters per voxel (must be > 0).
 * @param origin     World-space origin [3]. Copied into grid.
 * @return 0 on success, -1 on invalid arguments or allocation failure.
 */
int srd_sdf_grid_init(srd_sdf_grid_t *grid, int nx, int ny, int nz,
                      float voxel_size, const float origin[3]);

/**
 * @brief Destroy a grid, freeing the values array.
 *
 * Safe to call with NULL or an already-destroyed grid.
 *
 * @param grid Grid to destroy (may be NULL).
 */
void srd_sdf_grid_destroy(srd_sdf_grid_t *grid);

/**
 * @brief Get the SDF value at voxel (x, y, z).
 *
 * @param grid Grid to query (may be NULL → returns SRD_SDF_OUTSIDE).
 * @param x,y,z Voxel coordinates. Out-of-bounds → returns SRD_SDF_OUTSIDE.
 * @return SDF value at the voxel.
 */
float srd_sdf_grid_get(const srd_sdf_grid_t *grid, int x, int y, int z);

/**
 * @brief Set the SDF value at voxel (x, y, z).
 *
 * No-op if grid is NULL or coordinates are out of bounds.
 *
 * @param grid Grid to modify (may be NULL).
 * @param x,y,z Voxel coordinates.
 * @param value SDF value to write.
 */
void srd_sdf_grid_set(srd_sdf_grid_t *grid, int x, int y, int z, float value);

/* ── Operations (srd_sdf_grid_ops.c) ───────────────────────────── */

/**
 * @brief Fill every voxel in the grid with a constant value.
 *
 * @param grid  Grid to fill (may be NULL → no-op).
 * @param value Value to write to all voxels.
 */
void srd_sdf_grid_fill(srd_sdf_grid_t *grid, float value);

/**
 * @brief Deep-copy a grid. Allocates a new values array for dst.
 *
 * @param dst Output grid (must not be NULL). Previous contents are overwritten.
 * @param src Source grid (must not be NULL).
 * @return 0 on success, -1 on error.
 */
int srd_sdf_grid_copy(srd_sdf_grid_t *dst, const srd_sdf_grid_t *src);

/**
 * @brief Count the number of voxels with negative SDF values.
 *
 * Useful for measuring room volume.
 *
 * @param grid Grid to query (may be NULL → returns 0).
 * @return Number of voxels with value < 0.
 */
int srd_sdf_grid_count_negative(const srd_sdf_grid_t *grid);

/* ── Stamp primitives (srd_sdf_grid_stamp.c) ──────────────────── */

/**
 * @brief Stamp a box SDF into the grid using CSG union (min).
 *
 * Carves the box interior into the grid. For each voxel, computes the
 * box SDF and takes the minimum with the current value.
 *
 * @param grid Grid to modify (may be NULL → no-op).
 * @param cx,cy,cz Box center in world space.
 * @param hx,hy,hz Box half-extents along each axis.
 */
void srd_sdf_grid_stamp_box(srd_sdf_grid_t *grid,
                            float cx, float cy, float cz,
                            float hx, float hy, float hz);

/**
 * @brief Subtract a box SDF from the grid using CSG subtraction (max(grid, -box)).
 *
 * Fills in the box region, making it solid. For each voxel, computes
 * max(current, -box_sdf).
 *
 * @param grid Grid to modify (may be NULL → no-op).
 * @param cx,cy,cz Box center in world space.
 * @param hx,hy,hz Box half-extents along each axis.
 */
void srd_sdf_grid_subtract_box(srd_sdf_grid_t *grid,
                               float cx, float cy, float cz,
                               float hx, float hy, float hz);

/**
 * @brief Stamp a sphere SDF into the grid using CSG union (min).
 *
 * @param grid Grid to modify (may be NULL → no-op).
 * @param cx,cy,cz Sphere center in world space.
 * @param radius   Sphere radius.
 */
void srd_sdf_grid_stamp_sphere(srd_sdf_grid_t *grid,
                               float cx, float cy, float cz,
                               float radius);

/**
 * @brief Subtract a sphere SDF from the grid using CSG subtraction.
 *
 * Makes the sphere region solid. For each voxel, computes
 * max(current, -sphere_sdf).
 *
 * @param grid Grid to modify (may be NULL → no-op).
 * @param cx,cy,cz Sphere center in world space.
 * @param radius   Sphere radius.
 */
void srd_sdf_grid_subtract_sphere(srd_sdf_grid_t *grid,
                                  float cx, float cy, float cz,
                                  float radius);

/* ── Inline coordinate helpers ─────────────────────────────────── */

/**
 * @brief Convert world-space coordinates to voxel indices.
 *
 * Truncates towards zero (floor for positive values).
 */
static inline void srd_sdf_grid_world_to_voxel(const srd_sdf_grid_t *grid,
                                                float wx, float wy, float wz,
                                                int *vx, int *vy, int *vz) {
    if (!grid || !vx || !vy || !vz) return;
    *vx = (int)((wx - grid->origin[0]) / grid->voxel_size);
    *vy = (int)((wy - grid->origin[1]) / grid->voxel_size);
    *vz = (int)((wz - grid->origin[2]) / grid->voxel_size);
}

/**
 * @brief Convert voxel indices to world-space coordinates.
 *
 * Returns the world position of the voxel center (not corner).
 */
static inline void srd_sdf_grid_voxel_to_world(const srd_sdf_grid_t *grid,
                                                int vx, int vy, int vz,
                                                float *wx, float *wy, float *wz) {
    if (!grid || !wx || !wy || !wz) return;
    *wx = grid->origin[0] + (float)vx * grid->voxel_size;
    *wy = grid->origin[1] + (float)vy * grid->voxel_size;
    *wz = grid->origin[2] + (float)vz * grid->voxel_size;
}

#ifdef __cplusplus
}
#endif

#endif /* SRD_SDF_GRID_H */

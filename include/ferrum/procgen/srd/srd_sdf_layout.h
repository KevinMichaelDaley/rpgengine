/**
 * @file srd_sdf_layout.h
 * @brief SDF layout representation: flat array of axis-aligned box SDFs.
 *
 * Replaces the grammar tree as the state representation for the SRD
 * optimiser. Each room or corridor is one srd_sdf_box_t with explicit
 * centre and half-extent parameters, cleanly separated from the discrete
 * structural state (which boxes exist, adjacency graph).
 *
 * Continuous parameters (cx, cz, hw, hd) are extracted into a libtorch
 * tensor for L-BFGS optimisation. The adjacency matrix and box types are
 * the discrete structural state modified by rewrite rules.
 */
#ifndef FERRUM_PROCGEN_SRD_SDF_LAYOUT_H
#define FERRUM_PROCGEN_SRD_SDF_LAYOUT_H

#include "ferrum/procgen/srd/srd_grammar.h"
#include "ferrum/procgen/srd/srd_room_type.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One axis-aligned box in the SDF layout.
 *
 * Represents a single room or corridor segment. The signed distance
 * field is:
 *   sdf(qx, qz) = max(|qx - cx| - hw, |qz - cz| - hd)
 * Negative inside, positive outside, zero on the boundary.
 *
 * @note Ownership: boxes are value types stored inline in srd_sdf_layout_t.
 *       No pointers, no heap allocation.
 */
typedef struct {
    float           cx;     /**< Centre position X (world units) */
    float           cz;     /**< Centre position Z (world units) */
    float           hw;     /**< Half-width along X axis */
    float           hd;     /**< Half-depth along Z axis */
    srd_room_type_t type;   /**< Semantic room type */
    uint32_t        flags;  /**< Bitmask of SRD_BOX_* flags */

    /**
     * @brief Door widths on each wall side (N=0, S=1, E=2, W=3).
     *
     * Zero means no door on that wall. Non-zero values are interpreted
     * by the critic and tile rasteriser as door openings.
     */
    float           door_width[SRD_MAX_DOORS];
} srd_sdf_box_t;

/**
 * @brief Complete SDF layout: boxes + adjacency graph.
 *
 * The adjacency matrix adj[i * SRD_MAX_BOXES + j] is nonzero iff boxes
 * i and j are logically connected (shared wall, corridor, or door).
 * It is always symmetric: adj[i][j] == adj[j][i].
 *
 * @note This struct is ~260 KB (512 boxes * 80 bytes + 512^2 adjacency).
 *       Stack allocation is possible but arena/heap is preferred for the
 *       main layout. Sandbox copies during candidate evaluation are
 *       allocated on the heap.
 *
 * @note Nullability: none. All fields are always valid after init.
 * @note Side effects: none. Pure data structure.
 */
typedef struct {
    srd_sdf_box_t boxes[SRD_MAX_BOXES]; /**< Box array, packed [0..n_boxes-1] */
    int           n_boxes;              /**< Number of active boxes */

    /**
     * @brief Flat adjacency matrix.
     *
     * adj[i * SRD_MAX_BOXES + j] != 0  ⟺  boxes i and j are connected.
     * Always kept symmetric. Entries for indices >= n_boxes are zero.
     */
    uint8_t       adj[SRD_MAX_BOXES * SRD_MAX_BOXES];

    /** @brief Layout world bounds (used by ClampToBounds repair rule). */
    float         bounds_w;
    float         bounds_h;
} srd_sdf_layout_t;

/* ── Lifecycle ──────────────────────────────────────────────────── */

/**
 * @brief Zero-initialise a layout. Sets n_boxes = 0, clears adjacency.
 *
 * @param[out] layout  Layout to initialise. Must not be NULL.
 */
void srd_sdf_layout_init(srd_sdf_layout_t *layout);

/**
 * @brief Populate a layout from an ASCII grid (flood-fill result).
 *
 * Creates one box per grid region. Centre and half-extents are computed
 * from the bounding box of each region's cells. Adjacency is set from
 * grid edges. Layout bounds are set from grid width/height.
 *
 * @param[out] layout  Layout to populate. Must be initialised.
 * @param[in]  grid    Parsed ASCII grid (from srd_grid_parse).
 * @return 0 on success, -1 on error (NULL args, too many regions).
 *
 * @note Ownership: layout does not take ownership of grid.
 * @note Side effects: none beyond writing to layout.
 */
int srd_sdf_layout_from_grid(srd_sdf_layout_t *layout,
                             const srd_grid_t *grid);

/**
 * @brief Deep-copy src into dst.
 *
 * @param[out] dst  Destination layout.
 * @param[in]  src  Source layout.
 *
 * @note Ownership: dst is independent of src after copy.
 */
void srd_sdf_layout_copy(srd_sdf_layout_t *dst,
                         const srd_sdf_layout_t *src);

/* ── Box operations ─────────────────────────────────────────────── */

/**
 * @brief Add a box to the layout.
 *
 * @param layout  Layout to modify.
 * @param box     Box to add (copied by value).
 * @return Index of the new box, or -1 if layout is full.
 *
 * @note Side effects: increments n_boxes. Adjacency for the new box
 *       is initialised to all-zero; caller must set edges explicitly.
 */
int srd_sdf_layout_add_box(srd_sdf_layout_t *layout,
                           const srd_sdf_box_t *box);

/**
 * @brief Remove a box from the layout.
 *
 * Shifts boxes after idx down by one and remaps the adjacency matrix.
 * All adjacency entries involving the removed box are cleared.
 *
 * @param layout  Layout to modify.
 * @param idx     Index of the box to remove.
 * @return 0 on success, -1 on error (invalid index).
 *
 * @note Side effects: all indices > idx shift down by 1. Callers
 *       holding box indices must update them after this call.
 */
int srd_sdf_layout_remove_box(srd_sdf_layout_t *layout, int idx);

/* ── Adjacency ──────────────────────────────────────────────────── */

/**
 * @brief Set adjacency between two boxes (symmetric).
 *
 * @param layout  Layout to modify.
 * @param i       First box index.
 * @param j       Second box index.
 * @param value   true to connect, false to disconnect.
 *
 * @note No-op if i == j or either index is out of range.
 */
void srd_sdf_layout_set_adj(srd_sdf_layout_t *layout,
                            int i, int j, bool value);

/**
 * @brief Query adjacency between two boxes.
 *
 * @param layout  Layout to query.
 * @param i       First box index.
 * @param j       Second box index.
 * @return true if connected, false otherwise (including out-of-range).
 */
bool srd_sdf_layout_get_adj(const srd_sdf_layout_t *layout, int i, int j);

/**
 * @brief Count the number of neighbours of a box.
 *
 * @param layout  Layout to query.
 * @param idx     Box index.
 * @return Number of boxes adjacent to idx, or 0 if idx is invalid.
 */
int srd_sdf_layout_adj_count(const srd_sdf_layout_t *layout, int idx);

/**
 * @brief Get the indices of all neighbours of a box.
 *
 * @param layout  Layout to query.
 * @param idx     Box index.
 * @param[out] out  Array to fill with neighbour indices.
 * @param max_out   Capacity of out.
 * @return Number of neighbours written, or 0 if idx invalid.
 */
int srd_sdf_layout_adj_list(const srd_sdf_layout_t *layout, int idx,
                            int *out, int max_out);

/* ── SDF evaluation ─────────────────────────────────────────────── */

/**
 * @brief Evaluate the signed distance field of a single box at (qx, qz).
 *
 * SDF is negative inside, positive outside, zero on the boundary.
 *   sdf = max(|qx - cx| - hw, |qz - cz| - hd)
 *
 * @param box  Box to evaluate.
 * @param qx   Query X position.
 * @param qz   Query Z position.
 * @return Signed distance value.
 */
float srd_sdf_box_eval(const srd_sdf_box_t *box, float qx, float qz);

/**
 * @brief Evaluate the smooth-min union SDF of all boxes at (qx, qz).
 *
 * Uses LogSumExp approximation:
 *   sdf_union = -T * log( sum_i exp(-sdf_i / T) )
 *
 * @param layout      Layout to evaluate.
 * @param qx          Query X position.
 * @param qz          Query Z position.
 * @param temperature Smoothing temperature (smaller = sharper boundary).
 * @return Approximate signed distance to the union of all boxes.
 */
float srd_sdf_layout_union(const srd_sdf_layout_t *layout,
                           float qx, float qz, float temperature);

/**
 * @brief Rasterise the layout into a soft occupancy grid.
 *
 * For each grid cell (gx, gz), computes:
 *   occ = 1 / (1 + exp(sdf_union(world_x, world_z) / temperature))
 *
 * World coords: cell (gx, gz) maps to (gx + 0.5, gz + 0.5).
 *
 * @param layout      Layout to rasterise.
 * @param grid_w      Width of output grid (cells).
 * @param grid_h      Height of output grid (cells).
 * @param temperature Smoothing temperature.
 * @param[out] out    Output buffer, size grid_w * grid_h, row-major.
 *
 * @note Ownership: caller owns the output buffer.
 * @note Side effects: writes to out only.
 */
void srd_sdf_layout_rasterize(const srd_sdf_layout_t *layout,
                              int grid_w, int grid_h,
                              float temperature, float *out);

/**
 * @brief Check whether two boxes overlap (positive SDF intersection).
 *
 * Two axis-aligned boxes overlap iff their projections overlap on both
 * axes simultaneously.
 *
 * @param a  First box.
 * @param b  Second box.
 * @return true if they overlap, false otherwise.
 */
bool srd_sdf_box_overlap(const srd_sdf_box_t *a, const srd_sdf_box_t *b);

/**
 * @brief Check whether box inner is fully contained within box outer.
 *
 * @param outer  Outer box.
 * @param inner  Inner box.
 * @return true if inner is fully inside outer.
 */
bool srd_sdf_box_contains(const srd_sdf_box_t *outer,
                          const srd_sdf_box_t *inner);

#ifdef __cplusplus
}
#endif
#endif /* FERRUM_PROCGEN_SRD_SDF_LAYOUT_H */

/**
 * @file probe_brick.h
 * @brief SDF-driven ternary brick probe placement (rpg-pjkb, feature 1).
 *
 * Replaces the uniform probe lattice with the Unity-APV-style subdivision the
 * placement survey recommends (ref/probe_placement_survey.md): a ternary brick
 * hierarchy over the scene AABB where a brick DESCENDS iff the scene SDF at
 * its centre satisfies |sdf| <= half the brick diagonal (the brick's volume can
 * touch geometry), is EMITTED under the same test minus deep-buried bricks
 * (buried_frac), and a parent fully covered by kept children is SUPPRESSED
 * (the voxel index would never reference it -- pure probe waste otherwise). Every kept brick contributes a 4x4x4 probe lattice
 * at spacing size/3; the exact 3:1 level nesting makes lattice points of
 * adjacent and nested bricks coincide, and coincident probes are deduplicated
 * so bricks SHARE boundary probes (the seam fix -- do not change the ratios).
 *
 * Headless and allocation-free beyond the caller arena: the SDF arrives as a
 * plain function pointer so the baked stream, the zone SDF, or an analytic test
 * field all plug in. Deterministic: identical config => identical output.
 *
 * Ownership: all output arrays are carved from the caller's arena; nothing to
 * free. Error semantics: returns false on NULL/invalid args or arena
 * exhaustion, leaving no partial output. No side effects beyond the arena.
 */
#ifndef FERRUM_PROBE_PLACE_PROBE_BRICK_H
#define FERRUM_PROBE_PLACE_PROBE_BRICK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/probe/probe_set.h"

struct arena; /* ferrum/memory/arena.h */

/** Hierarchy depth cap: coarse + up to 3 ternary refinements (27x per axis). */
#define PROBE_BRICK_MAX_LEVELS 4

/**
 * @brief Placement parameters. All fields must be set (zero-init then fill).
 *
 * @c sdf is sampled at brick centres; it must return the SIGNED distance to the
 * nearest surface (negative inside). @c sdf_user is passed through untouched.
 * @c fill_empty keeps coarsest-level bricks whose keep test fails (pure open
 * air), guaranteeing coverage for mid-air dynamic objects.
 */
typedef struct probe_brick_config {
    float aabb_min[3];      /**< placement bounds, min corner (world m). */
    float aabb_max[3];      /**< placement bounds, max corner (world m). */
    float coarse_brick;     /**< coarsest brick edge (m); level-k edge = this/3^k. */
    int   levels;           /**< hierarchy depth, 1..PROBE_BRICK_MAX_LEVELS. */
    int   fill_empty;       /**< 1 = keep failing coarse bricks (open-air cover). */
    float buried_frac;      /**< >0: cull bricks whose centre is deeper inside
                             *   geometry than this fraction of their probe
                             *   spacing (children of culled-but-near bricks are
                             *   still visited, so thick-wall FACES survive).
                             *   <=0 = keep buried bricks (old behaviour). */
    float (*sdf)(const float p[3], void *user); /**< signed distance field. */
    void *sdf_user;         /**< opaque context for @c sdf (nullable). */
} probe_brick_config_t;

/**
 * @brief One kept brick: its box, level, and its 4x4x4 probe lattice as
 *        indices into the output probe set (dedup makes probes shared between
 *        bricks, so indices -- not contiguous ranges). Index order is
 *        probe_idx[(k*4 + j)*4 + i] for lattice point min + (i,j,k)*size/3.
 */
typedef struct probe_brick {
    float    min[3];         /**< brick min corner (world m). */
    float    size;           /**< brick edge length (m). */
    int32_t  level;          /**< 0 = coarsest. */
    uint32_t probe_idx[64];  /**< probe-set indices of the 4x4x4 lattice. */
} probe_brick_t;

/**
 * @brief Run the subdivision and emit the deduplicated probe set + bricks.
 *
 * @param cfg          placement parameters (see probe_brick_config_t).
 * @param arena        backing for ALL output allocations.
 * @param out_set      filled as an unstructured point set (grid_dim = 0,0,0).
 * @param out_bricks   receives the kept-brick array (NULL when none kept).
 * @param out_n_bricks receives the kept-brick count.
 * @return true on success (zero kept bricks is success); false on NULL args,
 *         levels outside [1, PROBE_BRICK_MAX_LEVELS], coarse_brick <= 0,
 *         NULL cfg->sdf, or arena exhaustion.
 */
bool probe_brick_place(const probe_brick_config_t *cfg, struct arena *arena,
                       probe_set_t *out_set, probe_brick_t **out_bricks,
                       uint32_t *out_n_bricks);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROBE_PLACE_PROBE_BRICK_H */

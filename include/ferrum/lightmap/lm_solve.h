/**
 * @file lm_solve.h
 * @brief Progressive-refinement form-factor radiosity solver (Southwell
 *        shooting) over a lightmap's luxels.
 *
 * Solves the radiosity system (I - rho F) B = E without ever materialising F,
 * by repeatedly shooting the patch with the greatest unshot exitance ("residual")
 * to its receivers. Receivers are found in the near field with a kd-tree over
 * luxel centres; each shot's point-to-point form factor is gated by an SVO
 * visibility ray and deposited as incident radiance into the receiver's SH, with
 * the reflected part (albedo * received) added to the receiver's own residual so
 * it shoots on a later iteration -- this is what produces multi-bounce colour
 * bleed. A partial-bake AABB gate restricts which receivers are updated so the
 * rest of the scene stays static.
 *
 * The residual is seeded (@ref lm_solver_init) from the luxel's current SH
 * irradiance (area-light direct, already baked) plus an optional analytic-light
 * seed (@ref lm_indirect output), each reflected by albedo. Ownership: the
 * solver borrows the lightmap, kd-tree and SVO; its residual/area/scratch
 * buffers come from the caller's arena. Nullability: @p svo may be NULL (no
 * occlusion). Offline / not perf-critical.
 */
#ifndef FERRUM_LIGHTMAP_LM_SOLVE_H
#define FERRUM_LIGHTMAP_LM_SOLVE_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/lightmap/lm_kdtree.h"
#include "ferrum/lightmap/lm_lightmap.h"
#include "ferrum/math/vec3.h"
#include "ferrum/memory/arena.h"
#include "ferrum/npc/npc_svo.h"
#include "ferrum/physics/aabb.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Tunables and the partial-bake gate for a radiosity solve. */
typedef struct lm_solve_params {
    float    near_radius;      /**< kd-tree influence radius for receivers. */
    uint32_t max_shots;        /**< Hard cap on shooting iterations. */
    float    residual_epsilon; /**< Stop when the max residual power drops below. */
    bool     use_region;       /**< Gate deposits to @c bake_region if true. */
    phys_aabb_t bake_region;   /**< Only luxels inside are relit (partial bake). */
} lm_solve_params_t;

/** Working set for a progressive radiosity solve over one luxel array. */
typedef struct lm_solver {
    lm_lightmap_t      *lm;       /**< Luxels being solved (SH mutated in place). */
    const lm_kdtree_t  *kdtree;   /**< kd-tree over the luxel-centre array. */
    const npc_svo_grid_t *svo;    /**< Occluders for visibility (may be NULL). */
    vec3_t             *residual; /**< Per-luxel unshot exitance (RGB). */
    float              *area;     /**< Per-luxel patch area. */
    uint32_t           *scratch;  /**< Neighbour-index scratch (>= n). */
    uint32_t            n;        /**< Luxel count (res_u * res_v). */
} lm_solver_t;

/**
 * @brief Initialise a solver over @p lm using @p kdtree (built over an array
 *        whose index i is luxel i's position). Seeds each luxel's residual from
 *        its SH irradiance plus @p seed_irradiance (may be NULL; res_u*res_v*3
 *        floats), reflected by albedo. Per-patch area comes from
 *        @p luxel_areas[i] when non-NULL (res_u*res_v floats, required for a
 *        multi-surface scene where patches differ in size); otherwise every
 *        patch uses @p uniform_area. Buffers are arena-allocated. Returns false
 *        on arena exhaustion.
 */
bool lm_solver_init(lm_solver_t *solver, lm_lightmap_t *lm,
                    const lm_kdtree_t *kdtree, const npc_svo_grid_t *svo,
                    const float *seed_irradiance, const float *luxel_areas,
                    float uniform_area, arena_t *arena);

/**
 * @brief Shoot the single highest-power residual patch to its near-field
 *        receivers. Returns false when no patch exceeds
 *        @p params->residual_epsilon (converged); true if a shot was performed.
 */
bool lm_solver_shoot_once(lm_solver_t *solver, const lm_solve_params_t *params);

/**
 * @brief Run @ref lm_solver_shoot_once until convergence or @c max_shots.
 *        Returns the number of shots performed.
 */
uint32_t lm_solver_run(lm_solver_t *solver, const lm_solve_params_t *params);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_SOLVE_H */

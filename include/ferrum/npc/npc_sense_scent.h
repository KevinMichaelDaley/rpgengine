/**
 * @file npc_sense_scent.h
 * @brief Wind field + scent grid for NPC olfaction.
 *
 * Scent emitters deposit concentrations on a coarse 3D grid.
 * The wind vector advects scent each tick.  SENSE_QUERY samples
 * the grid via trilinear interpolation to determine what an NPC smells.
 */
#ifndef FERRUM_NPC_SENSE_SCENT_H
#define FERRUM_NPC_SENSE_SCENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* ── Constants ──────────────────────────────────────────────────── */

#define NPC_SCENT_GRID_RES 32
#define NPC_SCENT_MAX_TYPES 5

/* ── Scent type enum ─────────────────────────────────────────────── */

typedef enum {
    NPC_SCENT_BLOOD = 0,
    NPC_SCENT_FOOD,
    NPC_SCENT_SMOKE,
    NPC_SCENT_SWEAT,
    NPC_SCENT_MONSTER_MUSK,
    NPC_SCENT_TYPE_COUNT
} npc_scent_type_t;

/* ── Scent field (3-D concentration grid) ───────────────────────── */

typedef struct {
    float   *grid;       /**< flat array: [z*res² + y*res + x]*NPC_SCENT_MAX_TYPES */
    float    wind[3];    /**< wind velocity in world units per second */
    uint32_t res;        /**< grid resolution (same in X, Y, Z) */
    float    cell_size;  /**< world-units per cell */
} npc_scent_field_t;

/* ── Scent emitter ───────────────────────────────────────────────── */

typedef struct {
    float            pos[3];          /**< world-space position */
    npc_scent_type_t type;            /**< scent type */
    float            intensity;       /**< amount deposited per emit call */
    uint32_t         remaining_ticks; /**< ticks until emitter expires */
} npc_scent_emitter_t;

/* ── Sample result ───────────────────────────────────────────────── */

typedef struct {
    npc_scent_type_t type;
    float            intensity;
} npc_scent_sample_t;

/* ── Lifecycle ──────────────────────────────────────────────────── */

/**
 * @brief Allocate and zero the scent grid.
 *
 * @param f         Pointer to caller-owned struct (zeroed on failure).
 * @param res       Grid resolution (e.g. 32 → 32×32×32).
 * @param cell_size World-units per cell edge.
 * @return true on success.
 */
bool npc_scent_field_init(npc_scent_field_t *f, uint32_t res, float cell_size);

/**
 * @brief Free the internal grid buffer and zero the struct.
 */
void npc_scent_field_destroy(npc_scent_field_t *f);

/* ── Emission ────────────────────────────────────────────────────── */

/**
 * @brief Deposit scent intensity at the grid cell containing @p em→pos.
 *
 * Adds @p em→intensity to the cell's concentration for @p em→type.
 * Out-of-bounds positions are clamped to the nearest valid cell.
 */
void npc_scent_emit(npc_scent_field_t *f, const npc_scent_emitter_t *em);

/* ── Advection ───────────────────────────────────────────────────── */

/**
 * @brief Move scent concentrations by the wind field over @p dt seconds.
 *
 * For every cell the source position is computed by backtracking along
 * the wind vector.  Scent arriving from outside the grid is lost.
 * Concentrations are damped by 5 % per call to simulate diffusion decay.
 */
void npc_scent_advect(npc_scent_field_t *f, float dt);

/* ── Sampling ────────────────────────────────────────────────────── */

/**
 * @brief Sample the scent field at a world-space point via trilinear
 *        interpolation.
 *
 * @param wx, wy, wz  World-space query position.
 * @param out          Filled with the strongest scent type and its
 *                     concentration.  Set to zero if nothing detected.
 * @return true if any scent was detected (out→intensity > 0).
 */
bool npc_scent_sample(const npc_scent_field_t *f,
                      float wx, float wy, float wz,
                      npc_scent_sample_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_NPC_SENSE_SCENT_H */

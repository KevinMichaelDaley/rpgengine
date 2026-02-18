#ifndef FERRUM_NET_REPLICATION_PREDICTION_TICK_H
#define FERRUM_NET_REPLICATION_PREDICTION_TICK_H

/**
 * @file prediction_tick.h
 * @brief Lightweight client-side prediction integrator.
 *
 * Runs at a fixed timestep (matching the server physics rate) on a
 * dedicated thread, decoupled from the render loop.  Each tick
 * integrates position from linear velocity and orientation from
 * angular velocity, applies gravity, and swaps the body pool double
 * buffers.  The render thread reads bodies_curr lock-free.
 *
 * Types: fr_prediction_tick_config_t, fr_prediction_tick_t (opaque).
 * Public functions: create, destroy, start, stop.
 */

#include "ferrum/math/vec3.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct phys_body_pool;

/**
 * @brief Configuration for the prediction tick integrator.
 */
typedef struct fr_prediction_tick_config {
    float    fixed_dt;    /**< Fixed timestep in seconds (e.g. 1/60). */
    vec3_t   gravity;     /**< Gravity acceleration (m/s²). */
    uint32_t max_bodies;  /**< Capacity of the body pool. */
} fr_prediction_tick_config_t;

/**
 * @brief Opaque prediction tick integrator.
 *
 * Owns a dedicated thread that ticks integration at a fixed rate.
 * The render thread reads from bodies_curr; the prediction thread
 * writes to bodies_next then swaps — double buffering keeps them
 * from colliding.
 */
typedef struct fr_prediction_tick fr_prediction_tick_t;

/**
 * @brief Create a prediction tick integrator.
 *
 * @param cfg Configuration (non-NULL, fixed_dt > 0).
 * @return Allocated integrator, or NULL on failure.
 *
 * Ownership: caller owns the returned pointer; free with destroy.
 * Nullability: returns NULL if cfg is NULL or invalid.
 */
fr_prediction_tick_t *fr_prediction_tick_create(
    const fr_prediction_tick_config_t *cfg);

/**
 * @brief Destroy a prediction tick integrator.
 *
 * Stops the thread if running, then frees.
 * @param pt Integrator to free (NULL-safe).
 */
void fr_prediction_tick_destroy(fr_prediction_tick_t *pt);

/**
 * @brief Start the prediction tick thread.
 *
 * Spawns a thread that runs integration at the configured fixed rate.
 * The thread reads/writes the body pool via double buffering.
 *
 * @param pt   Integrator (non-NULL).
 * @param pool Body pool to integrate (non-NULL, must outlive the thread).
 * @return true on success, false if already running or thread creation fails.
 */
bool fr_prediction_tick_start(fr_prediction_tick_t *pt,
                              struct phys_body_pool *pool);

/**
 * @brief Stop the prediction tick thread.
 *
 * Signals the thread to exit and joins it.  Safe to call if not running.
 *
 * @param pt Integrator (non-NULL).
 */
void fr_prediction_tick_stop(fr_prediction_tick_t *pt);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_NET_REPLICATION_PREDICTION_TICK_H */

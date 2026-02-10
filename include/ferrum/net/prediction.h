/**
 * @file prediction.h
 * @brief Client-side prediction and server reconciliation.
 *
 * Model:
 *   - Client stores inputs in a ring buffer keyed by tick.
 *   - Each tick, client applies the local input to advance the
 *     predicted state (optimistic simulation).
 *   - When a server-authoritative state arrives for a past tick:
 *     1. Rewind to the server state.
 *     2. Replay all unconfirmed inputs (ticks after the confirmed one).
 *     3. Compare replayed result to the old prediction.
 *     4. If error > snap_threshold → hard snap.
 *        If error > blend_threshold → blend old toward replayed.
 *        Otherwise → no correction.
 *
 * The simulation step is provided as a callback so this module is
 * decoupled from the physics engine.
 *
 * Ownership: all storage is caller-provided.  No dynamic allocation.
 * NULL-safe.
 */

#ifndef FERRUM_NET_PREDICTION_H
#define FERRUM_NET_PREDICTION_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Status codes ──────────────────────────────────────────────── */

#define NET_PREDICT_OK            0
#define NET_PREDICT_ERR_INVALID  -1

/* ── Types ─────────────────────────────────────────────────────── */

/**
 * @brief Predicted entity state: position + velocity.
 *
 * Kept simple — extend as needed for rotation, etc.
 */
typedef struct net_predict_state {
    float position[3];
    float velocity[3];
} net_predict_state_t;

/**
 * @brief A single input sample stored in the ring buffer.
 */
typedef struct net_predict_input {
    uint32_t tick;      /**< Tick this input was generated for. */
    float move[3];      /**< Movement input vector. */
} net_predict_input_t;

/**
 * @brief Ring buffer of timestamped inputs.
 *
 * Caller provides the backing array.
 */
typedef struct net_predict_input_ring {
    net_predict_input_t *entries; /**< Caller-owned ring storage. */
    uint32_t capacity;           /**< Ring size. */
    uint32_t write_idx;          /**< Next write position. */
    uint32_t count;              /**< Entries currently stored. */
} net_predict_input_ring_t;

/**
 * @brief Simulation step callback.
 *
 * Called once per tick during replay.  Must advance state by one tick
 * given the input.
 *
 * @param state  State to advance in-place.
 * @param input  Input for this tick.
 * @param user   User context pointer.
 */
typedef void (*net_predict_sim_fn)(net_predict_state_t *state,
                                   const net_predict_input_t *input,
                                   void *user);

/**
 * @brief Configuration for prediction/reconciliation.
 */
typedef struct net_predict_config {
    float snap_threshold;   /**< Error above this → hard snap (units). */
    float blend_threshold;  /**< Error below this → no correction. */
    float blend_rate;       /**< Blend factor [0,1] for soft correction. */
} net_predict_config_t;

/**
 * @brief Prediction context: state, input ring, config, and callbacks.
 */
typedef struct net_predict_ctx {
    net_predict_state_t predicted;     /**< Current predicted state. */
    uint32_t predicted_tick;           /**< Tick the prediction is at. */
    uint32_t confirmed_tick;           /**< Last server-confirmed tick. */
    net_predict_input_ring_t input_ring; /**< Input history. */
    net_predict_config_t config;       /**< Thresholds and blend rate. */
    net_predict_sim_fn sim_step;       /**< Simulation callback. */
    void *sim_user;                    /**< User context for callback. */
} net_predict_ctx_t;

/* ── Public API: input ring ────────────────────────────────────── */

/**
 * @brief Initialize an input ring buffer.
 *
 * @param ring     Ring to initialize (non-NULL).
 * @param entries  Caller-owned storage array.
 * @param capacity Number of entries in the array.
 */
void net_predict_input_ring_init(net_predict_input_ring_t *ring,
                                 net_predict_input_t *entries,
                                 uint32_t capacity);

/**
 * @brief Push an input into the ring.
 *
 * Overwrites the oldest entry if the ring is full.
 *
 * @param ring   Input ring (non-NULL).
 * @param input  Input to store (non-NULL).
 * @return NET_PREDICT_OK on success, NET_PREDICT_ERR_INVALID if NULL.
 */
int net_predict_input_ring_push(net_predict_input_ring_t *ring,
                                const net_predict_input_t *input);

/**
 * @brief Get an input by tick number.
 *
 * @param ring  Input ring (non-NULL).
 * @param tick  Tick to look up.
 * @return Pointer to the input, or NULL if not found / ring is NULL.
 */
const net_predict_input_t *net_predict_input_ring_get(
    const net_predict_input_ring_t *ring, uint32_t tick);

/* ── Public API: prediction context ────────────────────────────── */

/**
 * @brief Initialize a prediction context.
 *
 * @param ctx       Context to initialize (non-NULL).
 * @param ring_buf  Caller-owned input ring storage.
 * @param ring_cap  Ring capacity.
 * @param config    Prediction config (non-NULL).
 * @param sim_step  Simulation callback (non-NULL).
 * @param sim_user  User context for callback (may be NULL).
 */
void net_predict_init(net_predict_ctx_t *ctx,
                      net_predict_input_t *ring_buf,
                      uint32_t ring_cap,
                      const net_predict_config_t *config,
                      net_predict_sim_fn sim_step,
                      void *sim_user);

/**
 * @brief Reconcile prediction with a server-authoritative state.
 *
 * 1. Rewinds to server_state at confirmed_tick.
 * 2. Replays inputs from (confirmed_tick+1) to predicted_tick.
 * 3. Compares replayed result to old predicted state.
 * 4. Applies snap or blend based on error magnitude.
 *
 * @param ctx            Prediction context (non-NULL).
 * @param server_state   Server-authoritative state (non-NULL).
 * @param confirmed_tick Tick the server state is for.
 * @return NET_PREDICT_OK on success, NET_PREDICT_ERR_INVALID if NULL.
 */
int net_predict_reconcile(net_predict_ctx_t *ctx,
                          const net_predict_state_t *server_state,
                          uint32_t confirmed_tick);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_PREDICTION_H */

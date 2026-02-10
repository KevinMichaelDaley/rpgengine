/**
 * @file snapshot_delta.h
 * @brief Snapshot baseline tracking and delta replication.
 *
 * Provides:
 *   - A quantized body type (net_snap_body_t) matching the physics
 *     snapshot body layout but with an explicit body_id.
 *   - Delta computation between two snapshots (changed-field bitmask).
 *   - Delta application to reconstruct target state from baseline + delta.
 *   - Per-client baseline tracker with snapshot history ring buffer.
 *
 * Ownership: all storage is caller-provided; no dynamic allocation.
 * NULL-safe: all public functions check for NULL inputs.
 */

#ifndef FERRUM_NET_SNAPSHOT_DELTA_H
#define FERRUM_NET_SNAPSHOT_DELTA_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Status codes ──────────────────────────────────────────────── */

#define NET_SNAP_OK               0
#define NET_SNAP_ERR_INVALID     -1
#define NET_SNAP_FULL            -2
#define NET_SNAP_BASELINE_EXPIRED -3

/* ── Changed-field bitmask ─────────────────────────────────────── */

#define NET_SNAP_CHANGED_POS     (1u << 0)  /**< Position changed. */
#define NET_SNAP_CHANGED_ORI     (1u << 1)  /**< Orientation changed. */
#define NET_SNAP_CHANGED_LINVEL  (1u << 2)  /**< Linear velocity changed. */
#define NET_SNAP_CHANGED_ANGVEL  (1u << 3)  /**< Angular velocity changed. */
#define NET_SNAP_CHANGED_FLAGS   (1u << 4)  /**< Flags or tier changed. */
#define NET_SNAP_CHANGED_DESTROY (1u << 7)  /**< Body was destroyed. */

/** All data fields changed (used for newly-spawned bodies). */
#define NET_SNAP_CHANGED_ALL     (NET_SNAP_CHANGED_POS | NET_SNAP_CHANGED_ORI | \
                                  NET_SNAP_CHANGED_LINVEL | NET_SNAP_CHANGED_ANGVEL | \
                                  NET_SNAP_CHANGED_FLAGS)

/* ── Types ─────────────────────────────────────────────────────── */

/**
 * @brief Quantized body state for network snapshots.
 *
 * Same field layout as phys_snapshot_body_t but with an explicit
 * body_id so bodies can be matched across snapshots.
 */
typedef struct net_snap_body {
    uint16_t body_id;            /**< Unique body identifier. */
    int16_t position[3];         /**< Quantized position (mm). */
    int16_t orientation[3];      /**< Smallest-3 quaternion (snorm16). */
    int16_t linear_vel[3];       /**< Quantized linear velocity. */
    int16_t angular_vel[3];      /**< Quantized angular velocity. */
    uint8_t flags;               /**< Body flags. */
    uint8_t tier;                /**< Simulation tier. */
} net_snap_body_t;

/**
 * @brief A snapshot: tick + array of quantized bodies.
 *
 * Caller owns the bodies array.
 */
typedef struct net_snapshot {
    uint64_t tick;               /**< World tick for this snapshot. */
    uint32_t body_count;         /**< Number of active bodies. */
    net_snap_body_t *bodies;     /**< Caller-owned body array. */
} net_snapshot_t;

/* ── Delta types ───────────────────────────────────────────────── */

/**
 * @brief One entry in a snapshot delta: the changed fields for one body.
 *
 * If changed_mask includes NET_SNAP_CHANGED_DESTROY, the body was
 * present in the baseline but absent from the current snapshot.
 * Otherwise, `data` holds the new values for changed fields.
 */
typedef struct net_snap_delta_entry {
    uint16_t body_id;            /**< Body this entry refers to. */
    uint8_t changed_mask;        /**< Bitmask of NET_SNAP_CHANGED_*. */
    net_snap_body_t data;        /**< New values (only changed fields valid). */
} net_snap_delta_entry_t;

/**
 * @brief Snapshot delta: the diff between a baseline and current snapshot.
 *
 * Caller provides the entries array; capacity limits max entries.
 */
typedef struct net_snapshot_delta {
    net_snap_delta_entry_t *entries;  /**< Caller-owned delta entries. */
    uint32_t capacity;               /**< Max entries. */
    uint32_t count;                  /**< Entries written by compute. */
    uint64_t base_tick;              /**< Baseline tick. */
    uint64_t cur_tick;               /**< Current snapshot tick. */
} net_snapshot_delta_t;

/* ── Baseline tracker ──────────────────────────────────────────── */

/**
 * @brief Per-client baseline tracker with snapshot history ring.
 *
 * Records recent snapshots in a ring buffer so that when the client
 * ACKs a snapshot tick, the baseline can be set to that snapshot's
 * state.  If the ACKed tick has been evicted from the ring, a
 * fallback to full baseline send is triggered.
 *
 * All storage is caller-provided.
 */
typedef struct net_snap_baseline {
    /* Current baseline state. */
    net_snapshot_t baseline;      /**< Baseline snapshot (body storage below). */
    uint64_t baseline_tick;       /**< Tick of current baseline (0 = none). */

    /* Snapshot history ring buffer. */
    net_snapshot_t *ring;         /**< Array of ring_capacity snapshots. */
    net_snap_body_t *ring_bodies; /**< Flat body storage for all ring slots. */
    uint32_t bodies_per_slot;     /**< Max bodies per ring slot. */
    uint32_t ring_capacity;       /**< Number of ring slots. */
    uint32_t ring_write;          /**< Next write index (wraps). */
    uint32_t ring_count;          /**< Slots currently occupied. */
} net_snap_baseline_t;

/* ── Public API: delta compute / apply ─────────────────────────── */

/**
 * @brief Compute the delta between a baseline and current snapshot.
 *
 * Iterates bodies by body_id.  Bodies in current but not baseline
 * are marked with NET_SNAP_CHANGED_ALL.  Bodies in baseline but not
 * current are marked with NET_SNAP_CHANGED_DESTROY.  Bodies in both
 * are compared field-by-field.
 *
 * @param baseline  Baseline snapshot (non-NULL).
 * @param current   Current snapshot (non-NULL).
 * @param delta     Output delta (non-NULL, entries pre-allocated).
 * @return NET_SNAP_OK on success,
 *         NET_SNAP_FULL if delta capacity exceeded,
 *         NET_SNAP_ERR_INVALID if any argument is NULL.
 *
 * Side effects: writes delta->entries, sets count/base_tick/cur_tick.
 */
int net_snapshot_delta_compute(const net_snapshot_t *baseline,
                               const net_snapshot_t *current,
                               net_snapshot_delta_t *delta);

/**
 * @brief Apply a delta to a snapshot, producing the target state.
 *
 * For each delta entry:
 *   - DESTROY entries: not applied (caller handles removal).
 *   - Changed fields overwrite the corresponding body fields.
 *   - Unchanged fields are preserved.
 *
 * Also updates snapshot->tick to delta->cur_tick.
 *
 * @param snapshot  Snapshot to modify in-place (non-NULL).
 * @param delta     Delta to apply (non-NULL).
 * @return NET_SNAP_OK on success,
 *         NET_SNAP_ERR_INVALID if any argument is NULL.
 *
 * Side effects: modifies snapshot bodies and tick.
 */
int net_snapshot_delta_apply(net_snapshot_t *snapshot,
                             const net_snapshot_delta_t *delta);

/* ── Public API: baseline tracker ──────────────────────────────── */

/**
 * @brief Initialize a baseline tracker with caller-provided storage.
 *
 * @param bl              Tracker to initialize (non-NULL).
 * @param baseline_bodies Body storage for the baseline snapshot.
 * @param max_bodies      Capacity of baseline_bodies.
 * @param ring            Ring buffer of snapshot headers.
 * @param ring_bodies     Flat body storage for ring (ring_cap * bodies_per_slot).
 * @param bodies_per_slot Max bodies per ring snapshot.
 * @param ring_cap        Number of ring buffer slots.
 *
 * Side effects: zeroes all storage.
 */
void net_snap_baseline_init(net_snap_baseline_t *bl,
                            net_snap_body_t *baseline_bodies,
                            uint32_t max_bodies,
                            net_snapshot_t *ring,
                            net_snap_body_t *ring_bodies,
                            uint32_t bodies_per_slot,
                            uint32_t ring_cap);

/**
 * @brief Record a snapshot into the history ring.
 *
 * Copies body data into the next ring slot (overwrites oldest if full).
 *
 * @param bl   Baseline tracker (non-NULL).
 * @param snap Snapshot to record (non-NULL, bodies array valid).
 * @return NET_SNAP_OK on success, NET_SNAP_ERR_INVALID if NULL.
 */
int net_snap_baseline_record(net_snap_baseline_t *bl,
                             const net_snapshot_t *snap);

/**
 * @brief Acknowledge a snapshot tick and advance the baseline.
 *
 * Searches the ring for the given tick.  If found, copies that
 * snapshot's body data into the baseline.  If not found (expired),
 * returns NET_SNAP_BASELINE_EXPIRED.
 *
 * @param bl   Baseline tracker (non-NULL).
 * @param tick Tick the client acknowledged.
 * @return NET_SNAP_OK if baseline advanced,
 *         NET_SNAP_BASELINE_EXPIRED if tick not in ring,
 *         NET_SNAP_ERR_INVALID if bl is NULL.
 */
int net_snap_baseline_ack(net_snap_baseline_t *bl, uint64_t tick);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_SNAPSHOT_DELTA_H */

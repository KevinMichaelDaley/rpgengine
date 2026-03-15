/**
 * @file edit_entity_version.h
 * @brief Entity version tracking for delta-compressed sync.
 *
 * Tracks a monotonic global version counter. Each entity mutation
 * stamps the entity's version. Deletions produce tombstones in a
 * bounded ring buffer. Clients send their last known version and
 * receive only entities changed since then.
 *
 * Public types: edit_version_tombstone_t, edit_version_state_t (2-type rule).
 */
#ifndef FERRUM_EDITOR_EDIT_ENTITY_VERSION_H
#define FERRUM_EDITOR_EDIT_ENTITY_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/** @brief Maximum tombstone ring buffer capacity. */
#define EDIT_VERSION_TOMBSTONE_CAP 4096

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief A tombstone entry recording a deleted entity and the version
 *        at which it was deleted.
 */
typedef struct edit_version_tombstone {
    uint32_t entity_id;  /**< Deleted entity's ID. */
    uint64_t version;    /**< Global version when deletion occurred. */
} edit_version_tombstone_t;

/**
 * @brief Version tracking state for delta sync.
 *
 * Ownership: init() allocates, destroy() frees.
 * Thread safety: only accessed from the main tick thread.
 */
typedef struct edit_version_state {
    uint64_t  version;           /**< Global monotonic counter (0 = no changes). */
    uint64_t *entity_version;    /**< Per-entity version (parallel to entity store). */
    uint32_t  entity_capacity;   /**< Size of entity_version array. */

    edit_version_tombstone_t *tombstones; /**< Ring buffer of deletion records. */
    uint32_t  tombstone_head;    /**< Next write slot (wraps at capacity). */
    uint32_t  tombstone_count;   /**< Entries used (capped at capacity). */
    uint32_t  tombstone_capacity; /**< Ring buffer capacity. */
} edit_version_state_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle (edit_entity_version.c)                                         */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize version state with given entity capacity.
 * @param state           State to initialize.
 * @param entity_capacity Number of entity slots (matches entity store).
 * @return true on success, false on allocation failure.
 */
bool edit_version_init(edit_version_state_t *state, uint32_t entity_capacity);

/**
 * @brief Free all memory owned by the version state.
 * @param state  State to destroy (NULL-safe).
 */
void edit_version_destroy(edit_version_state_t *state);

/**
 * @brief Stamp an entity as modified.
 *
 * Increments the global version and sets entity_version[entity_id]
 * to the new version. No-op if entity_id >= entity_capacity.
 *
 * @param state      Version state.
 * @param entity_id  Entity that was modified.
 */
void edit_version_stamp(edit_version_state_t *state, uint32_t entity_id);

/**
 * @brief Record a deletion tombstone.
 *
 * Increments the global version and writes a tombstone entry into
 * the ring buffer. If the ring is full, the oldest entry is overwritten.
 *
 * @param state      Version state.
 * @param entity_id  Entity that was deleted.
 */
void edit_version_tombstone(edit_version_state_t *state, uint32_t entity_id);

/* ------------------------------------------------------------------------ */
/* Queries (edit_entity_version_query.c)                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief Check if a full resync is needed.
 *
 * Returns true if since_version is 0 (never synced) or if the
 * tombstone ring has wrapped past since_version (oldest tombstone
 * version > since_version while ring is full).
 *
 * @param state          Version state.
 * @param since_version  Client's last known version.
 * @return true if full resync required.
 */
bool edit_version_needs_full_resync(const edit_version_state_t *state,
                                     uint64_t since_version);

/**
 * @brief Count entities changed since a given version.
 * @param state          Version state.
 * @param since_version  Count entities with version > since_version.
 * @return Number of changed entities.
 */
uint32_t edit_version_count_changed(const edit_version_state_t *state,
                                     uint64_t since_version);

/**
 * @brief Get IDs of entities changed since a given version.
 *
 * Fills out_ids in ascending entity ID order.
 *
 * @param state          Version state.
 * @param since_version  Include entities with version > since_version.
 * @param out_ids        Output array for entity IDs.
 * @param max_ids        Maximum number of IDs to write.
 * @return Number of IDs written.
 */
uint32_t edit_version_get_changed_ids(const edit_version_state_t *state,
                                       uint64_t since_version,
                                       uint32_t *out_ids, uint32_t max_ids);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_ENTITY_VERSION_H */

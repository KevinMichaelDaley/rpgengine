#ifndef FERRUM_NET_GHOST_TABLE_H
#define FERRUM_NET_GHOST_TABLE_H

/** @file
 * @brief Ghost table: server entity → client entity mapping.
 *
 * Maps server-authoritative entity IDs (uint32_t) to client-local
 * entity handles (index + generation).  Used by the replication
 * system to track which server entities have been "ghosted" to a
 * given client and what local entity they map to.
 *
 * Ownership: caller provides the backing storage array.
 * No dynamic allocation; fixed-capacity linear scan.
 *
 * All public functions are NULL-safe.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Status codes ──────────────────────────────────────────────── */

#define NET_GHOST_OK          0
#define NET_GHOST_NOT_FOUND   1
#define NET_GHOST_FULL        2
#define NET_GHOST_DUPLICATE   3
#define NET_GHOST_ERR_INVALID -1

/* ── Types ─────────────────────────────────────────────────────── */

/**
 * @brief Client-local entity handle with generation for stale detection.
 */
typedef struct net_ghost_entity {
    uint32_t index;       /**< Dense pool index of the local entity. */
    uint32_t generation;  /**< Generation counter for validity checks. */
} net_ghost_entity_t;

/**
 * @brief A single entry in the ghost table.
 *
 * Maps one server entity ID to one client-local entity handle.
 * Entries are packed densely; unused entries have `used == 0`.
 */
typedef struct net_ghost_entry {
    uint32_t server_id;         /**< Server-authoritative entity ID. */
    net_ghost_entity_t local;   /**< Corresponding client-local entity. */
    uint8_t used;               /**< Non-zero if this entry is active. */
} net_ghost_entry_t;

/* ── Ghost table ───────────────────────────────────────────────── */

/**
 * @brief Fixed-capacity ghost table mapping server IDs to local entities.
 *
 * Caller owns the backing `entries` array.  No heap allocation.
 */
typedef struct net_ghost_table {
    net_ghost_entry_t *entries;  /**< Caller-owned entry storage. */
    uint32_t capacity;           /**< Maximum number of entries. */
    uint32_t count;              /**< Current number of active entries. */
} net_ghost_table_t;

/* ── Public API ────────────────────────────────────────────────── */

/**
 * @brief Initialize a ghost table with caller-provided storage.
 *
 * @param table     Table to initialize (non-NULL).
 * @param entries   Caller-owned array of `capacity` entries.
 * @param capacity  Maximum number of ghost mappings.
 *
 * Side effects: zeroes all entries.
 */
void net_ghost_table_init(net_ghost_table_t *table,
                          net_ghost_entry_t *entries,
                          uint32_t capacity);

/**
 * @brief Create a ghost mapping: server_id → local entity.
 *
 * @param table      Ghost table (non-NULL).
 * @param server_id  Server-authoritative entity ID.
 * @param local      Client-local entity handle.
 * @return NET_GHOST_OK on success,
 *         NET_GHOST_DUPLICATE if server_id already mapped,
 *         NET_GHOST_FULL if table at capacity,
 *         NET_GHOST_ERR_INVALID if table is NULL.
 */
int net_ghost_table_create(net_ghost_table_t *table,
                           uint32_t server_id,
                           net_ghost_entity_t local);

/**
 * @brief Lookup a ghost mapping by server entity ID.
 *
 * @param table      Ghost table (non-NULL).
 * @param server_id  Server entity ID to look up.
 * @param out        Output: local entity handle (non-NULL).
 * @return NET_GHOST_OK if found,
 *         NET_GHOST_NOT_FOUND if no mapping exists,
 *         NET_GHOST_ERR_INVALID if table or out is NULL.
 */
int net_ghost_table_lookup(const net_ghost_table_t *table,
                           uint32_t server_id,
                           net_ghost_entity_t *out);

/**
 * @brief Destroy a ghost mapping by server entity ID.
 *
 * @param table      Ghost table (non-NULL).
 * @param server_id  Server entity ID to remove.
 * @return NET_GHOST_OK if removed,
 *         NET_GHOST_NOT_FOUND if not mapped,
 *         NET_GHOST_ERR_INVALID if table is NULL.
 */
int net_ghost_table_destroy(net_ghost_table_t *table,
                            uint32_t server_id);

/**
 * @brief Clear all ghost mappings from the table.
 *
 * @param table  Ghost table (non-NULL, no-op if NULL).
 */
void net_ghost_table_clear(net_ghost_table_t *table);

/**
 * @brief Return the number of active ghost mappings.
 *
 * @param table  Ghost table (non-NULL).
 * @return Active entry count, or 0 if table is NULL.
 */
uint32_t net_ghost_table_count(const net_ghost_table_t *table);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_GHOST_TABLE_H */

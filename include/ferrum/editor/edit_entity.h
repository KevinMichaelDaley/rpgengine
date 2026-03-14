/**
 * @file edit_entity.h
 * @brief Editor entity storage.
 *
 * Simple flat-array entity store for the editor. Each entity has position,
 * rotation (euler), scale, and a type tag. Entities are addressed by ID
 * (array index). Inactive slots can be reclaimed by create().
 *
 * Thread safety: only mutated from the main tick thread during drain.
 */
#ifndef FERRUM_EDITOR_EDIT_ENTITY_H
#define FERRUM_EDITOR_EDIT_ENTITY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ferrum/entity/entity_attrs.h"
#include "ferrum/math/quat.h"

/* ------------------------------------------------------------------------ */
/* Constants                                                                 */
/* ------------------------------------------------------------------------ */

#define EDIT_ENTITY_TYPE_BOX       0
#define EDIT_ENTITY_TYPE_SPHERE    1
#define EDIT_ENTITY_TYPE_CAPSULE   2
#define EDIT_ENTITY_TYPE_MARKER    3
#define EDIT_ENTITY_TYPE_MESH      4
#define EDIT_ENTITY_TYPE_HALFSPACE 5
#define EDIT_ENTITY_INVALID_ID   UINT32_MAX

/** @brief Maximum number of entity types in the registry. */
#define EDIT_ENTITY_TYPE_MAX     32

/** @brief Maximum length of an entity name (including null terminator). */
#define EDIT_ENTITY_NAME_MAX 256

/** @brief Maximum length of a material path. */
#define EDIT_MATERIAL_PATH_MAX 256

/** @brief Material slot indices. */
#define EDIT_MATERIAL_SLOT_ALBEDO    0
#define EDIT_MATERIAL_SLOT_NORMAL    1
#define EDIT_MATERIAL_SLOT_ROUGHNESS 2
#define EDIT_MATERIAL_SLOT_METALLIC  3
#define EDIT_MATERIAL_SLOT_EMISSIVE  4
#define EDIT_MATERIAL_SLOT_COUNT     5

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief A single editor entity.
 */
typedef struct edit_entity {
    float    pos[3];          /**< World position. */
    float    rot[3];          /**< Euler rotation cache in degrees (display only).
                                   Derived from orientation; do not write directly
                                   without also updating orientation. */
    float    scale[3];        /**< Per-axis scale factors. */
    quat_t   orientation;     /**< Authoritative rotation (unit quaternion). */
    float    pivot_offset[3]; /**< Local-space pivot offset for transforms. */
    uint32_t type;         /**< Entity type (EDIT_ENTITY_TYPE_*). */
    uint32_t body_index;   /**< Physics body index (UINT32_MAX = none). */
    bool     active;       /**< Whether this slot is in use. */
    bool     pending_delete; /**< Queued for server-side deletion (greyed in outliner). */
    char     name[EDIT_ENTITY_NAME_MAX]; /**< Optional display name (empty = unnamed). */
    /** Material slot paths (empty = no material assigned). */
    char     materials[EDIT_MATERIAL_SLOT_COUNT][EDIT_MATERIAL_PATH_MAX];
    /** Dynamic key-value attributes for gameplay scripts. */
    entity_attrs_t attrs;
    /** Refresh generation — set by entity list refresh to track staleness. */
    uint32_t refresh_gen;
} edit_entity_t;

/**
 * @brief Flat-array entity store with O(1) freelist allocation.
 *
 * Ownership: init() allocates, destroy() frees.
 * The freelist is a LIFO stack of free slot indices, enabling O(1)
 * create and remove even at million-entity capacities.
 */
typedef struct edit_entity_store {
    edit_entity_t *entities;     /**< Array of entity slots (mmap'd). */
    uint32_t      *freelist;     /**< Stack of free slot indices. */
    size_t         entities_bytes; /**< Size of mmap'd region in bytes. */
    uint32_t       capacity;     /**< Total number of slots. */
    uint32_t       free_count;   /**< Number of entries in the freelist. */
    uint32_t       active_count; /**< Number of active entities (cached). */
} edit_entity_store_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle (edit_entity_store.c)                                           */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize the entity store.
 * @param store     Store to initialize.
 * @param capacity  Maximum number of entities.
 * @return true on success.
 */
bool edit_entity_store_init(edit_entity_store_t *store, uint32_t capacity);

/**
 * @brief Free all memory owned by the store.
 * @param store  Store to destroy.
 */
void edit_entity_store_destroy(edit_entity_store_t *store);

/**
 * @brief Create a new entity. Finds the first inactive slot.
 * @param store  Entity store.
 * @param type   Entity type (EDIT_ENTITY_TYPE_*).
 * @return Entity ID (slot index), or EDIT_ENTITY_INVALID_ID if full.
 *
 * The new entity is initialized with pos=(0,0,0), rot=(0,0,0), scale=(1,1,1).
 */
uint32_t edit_entity_store_create(edit_entity_store_t *store, uint32_t type);

/**
 * @brief Remove an entity (mark slot inactive).
 * @param store  Entity store.
 * @param id     Entity ID to remove.
 * @return true if removed, false if already inactive or out of range.
 */
bool edit_entity_store_remove(edit_entity_store_t *store, uint32_t id);

/* ------------------------------------------------------------------------ */
/* Access (edit_entity_store_access.c)                                       */
/* ------------------------------------------------------------------------ */

/**
 * @brief Get a read-only pointer to an entity.
 * @param store  Entity store.
 * @param id     Entity ID.
 * @return Pointer to entity, or NULL if inactive/out of range.
 */
const edit_entity_t *edit_entity_store_get(const edit_entity_store_t *store,
                                            uint32_t id);

/**
 * @brief Get a mutable pointer to an entity.
 * @param store  Entity store.
 * @param id     Entity ID.
 * @return Pointer to entity, or NULL if inactive/out of range.
 */
edit_entity_t *edit_entity_store_get_mut(edit_entity_store_t *store,
                                          uint32_t id);

/**
 * @brief Restore a previously removed entity from a snapshot.
 *
 * Re-activates slot `id` with the given snapshot data. Used for undo of delete.
 *
 * @param store     Entity store.
 * @param id        Slot to restore into.
 * @param snapshot  Entity data to copy in.
 * @return true on success, false if id out of range or slot already active.
 */
bool edit_entity_store_restore(edit_entity_store_t *store, uint32_t id,
                                const edit_entity_t *snapshot);

/**
 * @brief Count active entities.
 * @param store  Entity store.
 * @return Number of active entities.
 */
uint32_t edit_entity_store_count(const edit_entity_store_t *store);

/**
 * @brief Find an active entity by name.
 * @param store  Entity store.
 * @param name   Name to search for (case-sensitive).
 * @return Entity ID, or EDIT_ENTITY_INVALID_ID if not found.
 */
uint32_t edit_entity_store_find_by_name(const edit_entity_store_t *store,
                                        const char *name);

/* ------------------------------------------------------------------------ */
/* Entity type registry (edit_entity_types.c)                                */
/* ------------------------------------------------------------------------ */

/**
 * @brief An entry in the entity type registry.
 */
typedef struct edit_entity_type_info {
    char     name[32];   /**< Type name (e.g., "box", "sphere"). */
    uint32_t type_id;    /**< Numeric type ID (EDIT_ENTITY_TYPE_*). */
} edit_entity_type_info_t;

/**
 * @brief Get the built-in entity type registry.
 * @param[out] count  Number of types in the registry.
 * @return Pointer to array of type info structs.
 */
const edit_entity_type_info_t *edit_entity_type_registry(uint32_t *count);

/**
 * @brief Look up a type ID by name.
 * @param name  Type name (e.g., "box").
 * @return Type ID, or UINT32_MAX if not found.
 */
uint32_t edit_entity_type_by_name(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_ENTITY_H */

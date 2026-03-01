/**
 * @file edit_script_env.h
 * @brief Script environment: entity snapshots, update buffer, env context.
 *
 * Defines the data structures the script thread reads (snapshots) and
 * writes (update blob), plus the unified script_env_t that scripts
 * (scripting or native C) interact with.
 *
 * Thread safety: snapshot arrays are written by the tick thread and read
 * by the script thread. The update blob is written by the script thread
 * and read by the tick thread. Synchronization is via atomic seq/ready
 * flags (managed by the runtime, not this module).
 *
 * Ownership: script_update_buffer_t owns its blob memory (malloc'd).
 * The script_env_t does NOT own the blob — it borrows a pointer.
 */
#ifndef FERRUM_EDITOR_EDIT_SCRIPT_ENV_H
#define FERRUM_EDITOR_EDIT_SCRIPT_ENV_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "ferrum/entity/entity_attrs.h"
#include "ferrum/editor/edit_entity.h"

/* Forward declarations. */
struct edit_cmd_ring;
struct script_runtime;

/* ------------------------------------------------------------------------ */
/* Attribute write (packed into update blob)                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief A single attribute write within an entity update.
 *
 * Stored inline in the update blob: header followed by `size` payload bytes.
 */
typedef struct script_attr_write {
    uint16_t key;   /**< Attribute ID (SCRIPT_KEY_* or user-defined). */
    uint8_t  type;  /**< SCRIPT_ATTR_* type tag. */
    uint8_t  size;  /**< Payload size in bytes (max 255). */
} script_attr_write_t;

/* ------------------------------------------------------------------------ */
/* Entity update (variable-length, packed in blob)                           */
/* ------------------------------------------------------------------------ */

/**
 * @brief Header for a single entity's update in the blob.
 *
 * Layout in blob: script_entity_update_t header, followed by
 * attr_count × (script_attr_write_t + payload bytes).
 */
typedef struct script_entity_update {
    uint32_t entity_id;    /**< Target entity ID. */
    uint32_t generation;   /**< ECS generation (0 for edit-only entities). */
    uint16_t attr_count;   /**< Number of attribute writes. */
    uint16_t total_size;   /**< Total bytes: header + all attr writes + payloads. */
} script_entity_update_t;

/* ------------------------------------------------------------------------ */
/* Entity snapshot (read-only view for script thread)                        */
/* ------------------------------------------------------------------------ */

/**
 * @brief Read-only snapshot of a single entity's state.
 *
 * Covers both edit entities and ECS entities. Built by the tick thread
 * and consumed by the script thread.
 */
typedef struct script_entity_snapshot {
    uint32_t entity_id;    /**< Entity ID (slot index for edit entities). */
    uint32_t generation;   /**< ECS generation (0 for edit-only). */
    uint8_t  active;       /**< Whether entity is active. */
    uint8_t  type;         /**< Entity type (EDIT_ENTITY_TYPE_*). */
    char     name[EDIT_ENTITY_NAME_MAX];
    float    pos[3];
    float    rot[3];
    float    scale[3];
    uint32_t body_index;
    char     materials[EDIT_MATERIAL_SLOT_COUNT][EDIT_MATERIAL_PATH_MAX];
    entity_attrs_t attrs;  /**< Dynamic gameplay attributes. */
} script_entity_snapshot_t;

/**
 * @brief View into the snapshot array.
 */
typedef struct script_entity_view {
    const script_entity_snapshot_t *entities;
    uint32_t count;
    uint32_t capacity;
} script_entity_view_t;

/* ------------------------------------------------------------------------ */
/* Double-buffered update blob                                               */
/* ------------------------------------------------------------------------ */

/**
 * @brief Double-buffered update blob for script→tick communication.
 *
 * The script thread writes to the back buffer (index 1), then swaps.
 * The tick thread reads from the front buffer (index 0).
 *
 * Ownership: init allocates blob memory, destroy frees it.
 */
typedef struct script_update_buffer {
    uint8_t  *blob[2];     /**< [0]=front (tick reads), [1]=back (script writes). */
    uint32_t  used[2];     /**< Bytes used in each blob. */
    uint32_t  capacity;    /**< Blob capacity in bytes. */
    _Alignas(64) atomic_uint ready; /**< Set by script thread after swap. */
} script_update_buffer_t;

/**
 * @brief Initialize the double-buffered update blob.
 * @param buf       Buffer to initialize.
 * @param capacity  Size of each blob in bytes.
 * @return true on success, false on allocation failure.
 *
 * Ownership: caller must call script_update_buffer_destroy().
 */
bool script_update_buffer_init(script_update_buffer_t *buf, uint32_t capacity);

/**
 * @brief Free blob memory.
 * @param buf  Buffer to destroy.
 */
void script_update_buffer_destroy(script_update_buffer_t *buf);

/**
 * @brief Swap back→front: front gets the script's writes, back is cleared.
 * @param buf  Buffer to swap.
 *
 * Called by the script thread after finishing a tick's writes.
 * Side effects: sets atomic ready flag, clears back buffer used count.
 */
void script_update_buffer_swap(script_update_buffer_t *buf);

/* ------------------------------------------------------------------------ */
/* Script environment                                                        */
/* ------------------------------------------------------------------------ */

/** Maximum entities in selection context. */
#define SCRIPT_ENV_MAX_SELECTION 64

/**
 * @brief Unified environment for scripting and native code.
 *
 * Provides read-only entity snapshots, a write-only update blob,
 * and context (cursor, selection). Scripts interact exclusively
 * through this struct.
 *
 * Ownership: does NOT own the blob or cmd_ring — borrows pointers.
 * Thread safety: used only on the script thread.
 */
typedef struct script_env {
    /** Read-only entity state (snapshot from last tick). */
    script_entity_view_t entities;

    /** Write: update blob pointer and tracking. */
    uint8_t  *update_blob;
    uint32_t  update_blob_used;
    uint32_t  update_blob_capacity;

    /** Write: edit commands (spawn, delete, group, etc.). */
    struct edit_cmd_ring *cmd_ring;

    /** Context: cursor position and rotation (read-only snapshot). */
    float    cursor_pos[3];
    float    cursor_rot[3];

    /** Context: current selection (read-only snapshot). */
    uint32_t selection_ids[SCRIPT_ENV_MAX_SELECTION];
    uint32_t selection_count;

    /** Back-pointer to runtime (for internal use). */
    struct script_runtime *runtime;
} script_env_t;

/**
 * @brief Initialize a script_env_t with an external blob buffer.
 *
 * Sets up the env to write updates into the provided blob.
 * Does NOT allocate memory.
 *
 * @param env       Environment to initialize.
 * @param blob      Update blob buffer (caller-owned).
 * @param capacity  Blob capacity in bytes.
 */
void script_env_init_blob(script_env_t *env, uint8_t *blob, uint32_t capacity);

/**
 * @brief Reset the environment for a new tick.
 *
 * Clears the update blob used count. Does not touch snapshot or context.
 *
 * @param env  Environment to reset.
 */
void script_env_reset(script_env_t *env);

/**
 * @brief Append an attribute write for an entity into the update blob.
 *
 * If an update header for this entity_id already exists as the LAST
 * update in the blob, the new attribute is appended to it. Otherwise
 * a new update header is created.
 *
 * @param env        Environment with update blob.
 * @param entity_id  Target entity ID.
 * @param generation ECS generation (0 for edit-only).
 * @param key        Attribute key (SCRIPT_KEY_*).
 * @param type       Attribute type (SCRIPT_ATTR_*).
 * @param data       Pointer to value data. Must not be NULL if size > 0.
 * @param size       Payload size in bytes (max 255).
 *
 * Side effects: advances update_blob_used. Silently fails if blob full.
 * Ownership: data is copied into the blob.
 */
void script_env_write_attr(script_env_t *env, uint32_t entity_id,
                           uint32_t generation, uint16_t key,
                           uint8_t type, const void *data, uint8_t size);

/**
 * @brief Read a dynamic attribute from a snapshot entity.
 *
 * Delegates to entity_attrs_get on the snapshot's attrs block.
 *
 * @param entity    Snapshot entity to read from.
 * @param key       Attribute key.
 * @param out_type  Output: attribute type. May be NULL.
 * @param out_size  Output: payload size. May be NULL.
 * @return Pointer to payload data, or NULL if not present.
 */
const void *script_entity_get_attr(const script_entity_snapshot_t *entity,
                                   uint16_t key, uint8_t *out_type,
                                   uint8_t *out_size);

/* ------------------------------------------------------------------------ */
/* Snapshot builder                                                          */
/* ------------------------------------------------------------------------ */

/**
 * @brief Build a snapshot array from the entity store.
 *
 * Iterates active entities in the store and copies their fields
 * (including dynamic attrs) into the output buffer.
 *
 * @param store     Entity store to snapshot.
 * @param out       Output buffer for snapshots.
 * @param capacity  Maximum number of snapshots to write.
 * @return Number of snapshots written.
 *
 * Side effects: none (read-only on store).
 */
uint32_t script_snapshot_build(const edit_entity_store_t *store,
                               script_entity_snapshot_t *out,
                               uint32_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_SCRIPT_ENV_H */

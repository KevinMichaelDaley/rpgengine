/**
 * @file edit_cmd_ctx.h
 * @brief Editor command context — shared state for command handlers.
 *
 * Command handlers access entity storage, selection, and undo stack
 * through this context. Stored as dispatch->user_data.
 *
 * Thread safety: only accessed from the main tick thread during drain.
 */
#ifndef FERRUM_EDITOR_EDIT_CMD_CTX_H
#define FERRUM_EDITOR_EDIT_CMD_CTX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* Forward declarations (full types in their own headers). */
struct edit_entity_store;
struct edit_selection;
struct edit_undo_stack;
struct edit_entity;
struct edit_physics_ctrl;
struct json_value;
struct edit_asset_registry;
struct mesh_edit;
struct edit_version_state;

/* ------------------------------------------------------------------------ */
/* Physics bridge callback                                                   */
/* ------------------------------------------------------------------------ */

/**
 * @brief Callback for bridging editor entity ops to the physics engine.
 *
 * The editor entity store manages logical entities; this bridge lets the
 * host application (e.g., demo_server) also create/destroy/move the
 * corresponding physics bodies.
 */
typedef struct edit_physics_bridge {
    /**
     * @brief Called after an entity is spawned.
     * @param user_data  Opaque context (e.g., demo_ctx_t pointer).
     * @param entity_id  Editor entity ID.
     * @param entity     Pointer to the newly created entity.
     * @return Physics body index to store on the entity, or UINT32_MAX.
     */
    uint32_t (*on_spawn)(void *user_data, uint32_t entity_id,
                         const struct edit_entity *entity);

    /**
     * @brief Called before an entity is deleted.
     * @param user_data   Opaque context.
     * @param entity_id   Editor entity ID.
     * @param body_index  Physics body index from the entity.
     */
    void (*on_delete)(void *user_data, uint32_t entity_id,
                      uint32_t body_index);

    /**
     * @brief Called after an entity is moved (position changed).
     * @param user_data   Opaque context.
     * @param entity_id   Editor entity ID.
     * @param body_index  Physics body index.
     * @param pos         New position [x, y, z].
     */
    void (*on_move)(void *user_data, uint32_t entity_id,
                    uint32_t body_index, const float pos[3]);

    /**
     * @brief Query which entities are touching a given entity.
     *
     * The server implements this by performing narrowphase collision
     * tests (sphere, box, capsule, convex, mesh) against other bodies.
     * Results are entity IDs (not body indices).
     *
     * When candidates/candidate_count are provided (non-NULL, >0),
     * only test those specific entities (group_mask optimization).
     * When candidates is NULL, test against all entities.
     *
     * @param user_data        Opaque context.
     * @param entity_id        Editor entity ID to test.
     * @param candidates       Optional array of entity IDs to test against.
     * @param candidate_count  Number of candidates (0 = test all).
     * @param out_entity_ids   Output array of touching entity IDs.
     * @param max_results      Maximum number of results.
     * @return Number of touching entities written to out_entity_ids.
     */
    uint32_t (*on_query_touching)(void *user_data,
                                   uint32_t entity_id,
                                   const uint32_t *candidates,
                                   uint32_t candidate_count,
                                   uint32_t *out_entity_ids,
                                   uint32_t max_results);

    /**
     * @brief Called when mesh data (FVMA) is available for a body.
     * @param user_data  Opaque context.
     * @param body_index Physics body index.
     * @param fvma_data  Serialized FVMA data (caller retains ownership).
     * @param fvma_size  Size in bytes.
     *
     * The host should copy the data if it needs to keep it.
     */
    void (*on_mesh_data)(void *user_data, uint32_t body_index,
                         const uint8_t *fvma_data, uint32_t fvma_size);

    /**
     * @brief Called to create a physics joint between two bodies.
     * @param user_data      Opaque context.
     * @param body_a         Physics body index of first entity.
     * @param body_b         Physics body index of second entity.
     * @param joint_type     Joint type (0=distance, 1=ball, 2=hinge).
     * @param local_anchor_a Anchor in body A's local space [3].
     * @param local_anchor_b Anchor in body B's local space [3].
     * @param axis           Joint axis in world space [3] (hinge only).
     * @return Joint index, or UINT32_MAX on failure.
     */
    uint32_t (*on_joint)(void *user_data,
                         uint32_t body_a, uint32_t body_b,
                         int joint_type,
                         const float local_anchor_a[3],
                         const float local_anchor_b[3],
                         const float axis[3]);

    /**
     * @brief Called to set physics material on a body.
     * @param user_data    Opaque context.
     * @param body_index   Physics body index.
     * @param friction     Surface friction coefficient (0–1+).
     * @param restitution  Coefficient of restitution (0–1).
     */
    void (*on_set_material)(void *user_data,
                            uint32_t body_index,
                            float friction, float restitution);

    void *user_data;  /**< Opaque context passed to all callbacks. */
} edit_physics_bridge_t;

/* ------------------------------------------------------------------------ */
/* Command type tags (for undo entries)                                      */
/* ------------------------------------------------------------------------ */

/**
 * @brief Command type tags used in undo entry forward/inverse_type fields.
 */
typedef enum edit_cmd_type {
    EDIT_CMD_TYPE_SPAWN        = 1,
    EDIT_CMD_TYPE_DELETE       = 2,
    EDIT_CMD_TYPE_MOVE         = 3,
    EDIT_CMD_TYPE_ROTATE       = 4,
    EDIT_CMD_TYPE_SCALE        = 5,
    EDIT_CMD_TYPE_GROUP_CREATE = 6,
    EDIT_CMD_TYPE_GROUP_DELETE = 7,
} edit_cmd_type_t;

/* ------------------------------------------------------------------------ */
/* Cursor position stack                                                     */
/* ------------------------------------------------------------------------ */

/** @brief Maximum depth of the cursor position stack. */
#define EDIT_CURSOR_STACK_MAX 16

/* ------------------------------------------------------------------------ */
/* Selection groups                                                          */
/* ------------------------------------------------------------------------ */

/** @brief Maximum number of named selection groups. */
#define EDIT_GROUP_MAX       64
/** @brief Maximum name length for a group (including & prefix and NUL). */
#define EDIT_GROUP_NAME_MAX  64
/** @brief Maximum entities per group. */
#define EDIT_GROUP_ENTRY_MAX 4096

/**
 * @brief A named selection group — snapshot of entity IDs.
 *
 * Name must start with '&'. Entity IDs are stored as a simple array.
 * Groups may have a pivot point (center of operations) and an optional
 * parent group for nesting.
 */
typedef struct edit_group {
    char     name[EDIT_GROUP_NAME_MAX];   /**< Group name (e.g. "&walls"). */
    uint32_t ids[EDIT_GROUP_ENTRY_MAX];   /**< Entity IDs in the group. */
    uint32_t count;                       /**< Number of entities. */
    bool     active;                      /**< True if slot is in use. */
    float    pivot[3];                    /**< Pivot point (avg of member positions). */
    char     parent[EDIT_GROUP_NAME_MAX]; /**< Parent group name ("" = no parent). */
} edit_group_t;

/* ------------------------------------------------------------------------ */
/* Command context                                                           */
/* ------------------------------------------------------------------------ */

/**
 * @brief Shared context for all entity command handlers.
 *
 * Stored as dispatch->user_data. Handlers cast user_data to this type.
 */
typedef struct edit_cmd_ctx {
    struct edit_entity_store  *entities;   /**< Entity storage. */
    struct edit_selection     *selection;  /**< Current selection set. */
    struct edit_undo_stack    *undo;      /**< Undo/redo stack. */
    edit_physics_bridge_t    *bridge;     /**< Physics bridge (NULL = no-op). */
    struct edit_physics_ctrl *physics;    /**< Physics sim control (NULL = no-op). */

    /** @brief Stack of saved cursor positions (LIFO). */
    float    cursor_stack[EDIT_CURSOR_STACK_MAX][3];
    /** @brief Number of positions currently on the cursor stack. */
    uint32_t cursor_stack_count;

    /** @brief Named selection groups (heap-allocated array, EDIT_GROUP_MAX slots). */
    edit_group_t *groups;
    /** @brief Number of allocated group slots. */
    uint32_t      group_capacity;

    /** @brief Asset registry (NULL if no asset directory configured). */
    struct edit_asset_registry *asset_registry;

    /** @brief Mesh editing context (NULL if mesh mode not initialized). */
    struct mesh_edit *mesh;

    /** @brief Script runtime (NULL if scripting not configured). */
    struct aegis_script_runtime *script_runtime;

    /** @brief Entity version tracking for delta sync (NULL = disabled). */
    struct edit_version_state *version;
} edit_cmd_ctx_t;

/* Forward declaration for JSON types. */
struct json_value;

/**
 * @brief Resolve an entity ID from a JSON value.
 *
 * Accepts either a number (direct ID) or a string (entity name lookup).
 * Returns EDIT_ENTITY_INVALID_ID (UINT32_MAX) if resolution fails.
 *
 * @param ctx       Command context (for entity store access).
 * @param id_val    JSON value from args (number or string).
 * @return Resolved entity ID, or UINT32_MAX.
 */
uint32_t edit_cmd_resolve_entity(const edit_cmd_ctx_t *ctx,
                                 const struct json_value *id_val);

/**
 * @brief Find a group by name.
 *
 * @param ctx   Command context.
 * @param name  Group name (must start with '&').
 * @return Pointer to the group, or NULL if not found.
 */
edit_group_t *edit_cmd_find_group(const edit_cmd_ctx_t *ctx,
                                  const char *name);

/**
 * @brief Check if an entity ID is a member of a group.
 *
 * @param grp  Group to search (NULL returns false).
 * @param id   Entity ID to check.
 * @return true if id is found in the group's ID list.
 */
bool edit_cmd_group_contains(const edit_group_t *grp, uint32_t id);

/**
 * @brief Resolve an optional group_mask argument from JSON args.
 *
 * Looks for "group_mask" string key in args. If present, finds the
 * group by name. If not present, returns NULL (no mask = accept all).
 * Sets *fail to true if the mask name is present but group not found.
 *
 * @param ctx   Command context.
 * @param args  JSON args object.
 * @param fail  Set to true if group_mask was specified but not found.
 * @return Pointer to group, or NULL if no mask or not found.
 */
const edit_group_t *edit_cmd_resolve_group_mask(
    const edit_cmd_ctx_t *ctx, const struct json_value *args, bool *fail);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_CMD_CTX_H */

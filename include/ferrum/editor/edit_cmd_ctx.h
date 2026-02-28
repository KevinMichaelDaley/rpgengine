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

#include <stdint.h>

/* Forward declarations (full types in their own headers). */
struct edit_entity_store;
struct edit_selection;
struct edit_undo_stack;
struct edit_entity;
struct edit_physics_ctrl;

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

    void *user_data;  /**< Opaque context passed to all callbacks. */
} edit_physics_bridge_t;

/* ------------------------------------------------------------------------ */
/* Command type tags (for undo entries)                                      */
/* ------------------------------------------------------------------------ */

/**
 * @brief Command type tags used in undo entry forward/inverse_type fields.
 */
typedef enum edit_cmd_type {
    EDIT_CMD_TYPE_SPAWN   = 1,
    EDIT_CMD_TYPE_DELETE  = 2,
    EDIT_CMD_TYPE_MOVE    = 3,
    EDIT_CMD_TYPE_ROTATE  = 4,
    EDIT_CMD_TYPE_SCALE   = 5,
} edit_cmd_type_t;

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

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_CMD_CTX_H */

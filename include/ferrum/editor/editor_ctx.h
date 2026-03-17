/**
 * @file editor_ctx.h
 * @brief Editor context — aggregates all editor subsystems.
 *
 * The editor context owns the I/O thread, command/response rings,
 * dispatch table, undo stack, selection, and entity store. It provides
 * init/shutdown lifecycle and a per-tick drain function.
 *
 * Thread safety: init/shutdown from any thread; drain from tick thread only.
 */
#ifndef FERRUM_EDITOR_EDITOR_CTX_H
#define FERRUM_EDITOR_EDITOR_CTX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/editor/edit_cmd_ring.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_dispatch.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_io_thread.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/edit_skeleton_registry.h"
#include "ferrum/editor/edit_undo.h"
#include "ferrum/editor/mesh/mesh_edit.h"

/* ------------------------------------------------------------------------ */
/* Configuration                                                             */
/* ------------------------------------------------------------------------ */

/**
 * @brief Editor initialization configuration.
 *
 * All fields have sensible defaults when zero-initialized.
 */
typedef struct editor_ctx_config {
    uint16_t edit_port;         /**< TCP port for edit protocol (0 = default 9100). */
    uint32_t max_entities;      /**< Max entity count (0 = 4096). */
    uint32_t undo_capacity;     /**< Undo stack depth (0 = 4096). */
    uint32_t ring_capacity;     /**< Ring buffer slots (0 = 1024). */
    uint32_t ring_payload_max;  /**< Max payload per ring slot (0 = 8192). */
    uint32_t dispatch_arena;    /**< Dispatch arena size (0 = 32768). */
    const char *asset_dir;      /**< Asset root directory (NULL = "asset_src"). */
} editor_ctx_config_t;

/* ------------------------------------------------------------------------ */
/* Context                                                                   */
/* ------------------------------------------------------------------------ */

/**
 * @brief Top-level editor context aggregating all subsystems.
 *
 * Ownership: editor_ctx_init() allocates everything; editor_ctx_shutdown() frees.
 */
typedef struct editor_ctx {
    edit_io_thread_t     io_thread;     /**< TCP I/O thread. */
    edit_cmd_ring_t      cmd_ring;      /**< Commands: I/O thread → tick thread. */
    edit_cmd_ring_t      resp_ring;     /**< Responses: tick thread → I/O thread. */
    edit_dispatch_t      dispatch;      /**< Command dispatch table. */
    edit_undo_stack_t    undo;          /**< Undo/redo stack. */
    edit_selection_t     selection;     /**< Entity selection set. */
    edit_entity_store_t  entities;      /**< Entity storage. */
    mesh_edit_t          mesh;          /**< Mesh editing subsystem. */
    edit_cmd_ctx_t       cmd_ctx;       /**< Handler context (pointers into above). */
    edit_skeleton_registry_t skeleton_registry; /**< Loaded skeleton storage. */

    /** @brief Entity version tracking for delta sync (heap-allocated). */
    struct edit_version_state *version_state;

    editor_ctx_config_t  config;        /**< Resolved configuration. */
    bool                 initialized;   /**< Guard against double init/shutdown. */
} editor_ctx_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize the editor context and start all subsystems.
 *
 * Allocates rings, dispatch, undo, selection, entity store, registers
 * default command handlers, and starts the I/O thread.
 *
 * @param ctx     Context to initialize.
 * @param config  Configuration (zero-init fields use defaults).
 * @return true on success, false on failure (partial init is cleaned up).
 */
bool editor_ctx_init(editor_ctx_t *ctx, const editor_ctx_config_t *config);

/**
 * @brief Shut down the editor and free all resources.
 *
 * Stops the I/O thread, destroys all subsystems, and zeros the context.
 *
 * @param ctx  Context to shut down.
 */
void editor_ctx_shutdown(editor_ctx_t *ctx);

/* ------------------------------------------------------------------------ */
/* Tick integration                                                          */
/* ------------------------------------------------------------------------ */

/**
 * @brief Drain pending commands from the ring and dispatch them.
 *
 * Called once per server tick (Stage 1, between RUDP drain and physics).
 * For each command: parse JSON, dispatch to handler, push response.
 *
 * @param ctx  Editor context.
 * @return Number of commands processed this tick.
 */
uint32_t editor_tick_drain(editor_ctx_t *ctx);

/**
 * @brief Set the physics bridge for editor commands.
 *
 * The bridge callbacks are invoked by spawn/delete/move commands to
 * create/destroy/teleport physics bodies alongside editor entities.
 * Pass NULL to disable bridging (default).
 *
 * @param ctx     Editor context.
 * @param bridge  Physics bridge (caller owns memory, must outlive ctx).
 */
void editor_ctx_set_bridge(editor_ctx_t *ctx, edit_physics_bridge_t *bridge);

/**
 * @brief Attach a physics simulation controller to the editor.
 *
 * Enables physics_pause/resume/step/reset commands.
 * Pass NULL to disable physics control (default).
 *
 * @param ctx      Editor context.
 * @param physics  Physics controller (caller owns memory, must outlive ctx).
 */
void editor_ctx_set_physics(editor_ctx_t *ctx, struct edit_physics_ctrl *physics);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDITOR_CTX_H */

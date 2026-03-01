/**
 * @file editor_ctx.c
 * @brief Editor context lifecycle — init and shutdown.
 *
 * Initializes all editor subsystems in order and wires them together.
 * On failure, partial initialization is cleaned up.
 */

#include "ferrum/editor/editor_ctx.h"
#include "ferrum/editor/edit_commands.h"
#include <stdio.h>
#include <string.h>

/** @brief Apply defaults to zero-valued config fields. */
static void resolve_config_(editor_ctx_config_t *cfg) {
    if (cfg->max_entities == 0)     cfg->max_entities     = 1000000;
    if (cfg->undo_capacity == 0)    cfg->undo_capacity    = EDIT_UNDO_DEFAULT_CAP;
    if (cfg->ring_capacity == 0)    cfg->ring_capacity    = EDIT_CMD_RING_DEFAULT_CAP;
    if (cfg->ring_payload_max == 0) cfg->ring_payload_max = 8192;
    if (cfg->dispatch_arena == 0)   cfg->dispatch_arena   = 32768;
}

bool editor_ctx_init(editor_ctx_t *ctx, const editor_ctx_config_t *config) {
    if (!ctx) return false;
    memset(ctx, 0, sizeof(*ctx));

    if (!config) return false;
    ctx->config = *config;
    resolve_config_(&ctx->config);

    /* Command and response rings. */
    if (!edit_cmd_ring_init(&ctx->cmd_ring, ctx->config.ring_capacity,
                            ctx->config.ring_payload_max)) {
        fprintf(stderr, "[editor_ctx] cmd_ring init failed\n");
        return false;
    }
    if (!edit_cmd_ring_init(&ctx->resp_ring, ctx->config.ring_capacity,
                            ctx->config.ring_payload_max)) {
        fprintf(stderr, "[editor_ctx] resp_ring init failed\n");
        edit_cmd_ring_destroy(&ctx->cmd_ring);
        return false;
    }

    /* Entity store, selection, undo stack. */
    if (!edit_entity_store_init(&ctx->entities, ctx->config.max_entities)) {
        fprintf(stderr, "[editor_ctx] entity_store init failed (cap=%u)\n",
                ctx->config.max_entities);
        edit_cmd_ring_destroy(&ctx->cmd_ring);
        edit_cmd_ring_destroy(&ctx->resp_ring);
        return false;
    }
    if (!edit_selection_init(&ctx->selection)) {
        fprintf(stderr, "[editor_ctx] selection init failed\n");
        edit_entity_store_destroy(&ctx->entities);
        edit_cmd_ring_destroy(&ctx->cmd_ring);
        edit_cmd_ring_destroy(&ctx->resp_ring);
        return false;
    }
    if (!edit_undo_init(&ctx->undo, ctx->config.undo_capacity,
                        EDIT_UNDO_DEFAULT_ARENA_MB * 1024 * 1024)) {
        fprintf(stderr, "[editor_ctx] undo init failed\n");
        edit_selection_destroy(&ctx->selection);
        edit_entity_store_destroy(&ctx->entities);
        edit_cmd_ring_destroy(&ctx->cmd_ring);
        edit_cmd_ring_destroy(&ctx->resp_ring);
        return false;
    }

    /* Wire command context. */
    ctx->cmd_ctx.entities  = &ctx->entities;
    ctx->cmd_ctx.selection = &ctx->selection;
    ctx->cmd_ctx.undo      = &ctx->undo;

    /* Mesh editing subsystem. */
    if (!mesh_edit_init(&ctx->mesh)) {
        fprintf(stderr, "[editor_ctx] mesh_edit init failed\n");
        edit_undo_destroy(&ctx->undo);
        edit_selection_destroy(&ctx->selection);
        edit_entity_store_destroy(&ctx->entities);
        edit_cmd_ring_destroy(&ctx->cmd_ring);
        edit_cmd_ring_destroy(&ctx->resp_ring);
        return false;
    }
    ctx->cmd_ctx.mesh = &ctx->mesh;

    /* Dispatch table. */
    if (!edit_dispatch_init(&ctx->dispatch, ctx->config.dispatch_arena,
                            &ctx->cmd_ctx)) {
        fprintf(stderr, "[editor_ctx] dispatch init failed\n");
        edit_undo_destroy(&ctx->undo);
        edit_selection_destroy(&ctx->selection);
        edit_entity_store_destroy(&ctx->entities);
        edit_cmd_ring_destroy(&ctx->cmd_ring);
        edit_cmd_ring_destroy(&ctx->resp_ring);
        return false;
    }
    edit_commands_register_all(&ctx->dispatch);

    /* I/O thread (last — depends on everything above). */
    if (!edit_io_start(&ctx->io_thread, ctx->config.edit_port,
                       &ctx->cmd_ring, &ctx->resp_ring)) {
        fprintf(stderr, "[editor_ctx] io_start failed (port=%u)\n",
                ctx->config.edit_port);
        edit_dispatch_destroy(&ctx->dispatch);
        edit_undo_destroy(&ctx->undo);
        edit_selection_destroy(&ctx->selection);
        edit_entity_store_destroy(&ctx->entities);
        edit_cmd_ring_destroy(&ctx->cmd_ring);
        edit_cmd_ring_destroy(&ctx->resp_ring);
        return false;
    }

    ctx->initialized = true;
    return true;
}

void editor_ctx_shutdown(editor_ctx_t *ctx) {
    if (!ctx || !ctx->initialized) return;

    /* Stop I/O thread first (it references the rings). */
    edit_io_stop(&ctx->io_thread);

    /* Destroy subsystems in reverse order. */
    edit_dispatch_destroy(&ctx->dispatch);
    mesh_edit_destroy(&ctx->mesh);
    edit_undo_destroy(&ctx->undo);
    edit_selection_destroy(&ctx->selection);
    edit_entity_store_destroy(&ctx->entities);
    edit_cmd_ring_destroy(&ctx->resp_ring);
    edit_cmd_ring_destroy(&ctx->cmd_ring);

    ctx->initialized = false;
}

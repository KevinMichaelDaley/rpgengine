/**
 * @file cmd_cursor.c
 * @brief Cursor position commands: push, pop, snap.
 *
 * cursor_push: save @cursor position onto the context cursor stack.
 * cursor_pop:  restore @cursor to last pushed position.
 * cursor_snap: move @cursor to a named entity, or to the centroid of
 *              the current selection if no entity_id given.
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"

#include <string.h>

/* ── cursor_push ──────────────────────────────────────────────────── */

bool cmd_cursor_push(edit_dispatch_t *d, const json_value_t *args,
                     json_value_t *result, json_arena_t *arena) {
    (void)args; (void)result; (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities) return false;

    /* Find @cursor entity. */
    uint32_t cid = edit_entity_store_find_by_name(ctx->entities, "@cursor");
    if (cid == EDIT_ENTITY_INVALID_ID) return false;
    const edit_entity_t *cur = edit_entity_store_get(ctx->entities, cid);
    if (!cur) return false;

    /* Check stack capacity. */
    if (ctx->cursor_stack_count >= EDIT_CURSOR_STACK_MAX) return false;

    /* Push current position. */
    uint32_t idx = ctx->cursor_stack_count++;
    ctx->cursor_stack[idx][0] = cur->pos[0];
    ctx->cursor_stack[idx][1] = cur->pos[1];
    ctx->cursor_stack[idx][2] = cur->pos[2];

    return true;
}

/* ── cursor_pop ───────────────────────────────────────────────────── */

bool cmd_cursor_pop(edit_dispatch_t *d, const json_value_t *args,
                    json_value_t *result, json_arena_t *arena) {
    (void)args; (void)result; (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities) return false;

    /* Stack must not be empty. */
    if (ctx->cursor_stack_count == 0) return false;

    /* Find @cursor entity. */
    uint32_t cid = edit_entity_store_find_by_name(ctx->entities, "@cursor");
    if (cid == EDIT_ENTITY_INVALID_ID) return false;
    edit_entity_t *cur = edit_entity_store_get_mut(ctx->entities, cid);
    if (!cur) return false;

    /* Pop and restore position. */
    uint32_t idx = --ctx->cursor_stack_count;
    cur->pos[0] = ctx->cursor_stack[idx][0];
    cur->pos[1] = ctx->cursor_stack[idx][1];
    cur->pos[2] = ctx->cursor_stack[idx][2];

    /* Notify bridge if configured. */
    if (ctx->bridge && ctx->bridge->on_move) {
        ctx->bridge->on_move(ctx->bridge->user_data, cid,
                             cur->body_index, cur->pos);
    }

    return true;
}

/* ── cursor_snap ──────────────────────────────────────────────────── */

bool cmd_cursor_snap(edit_dispatch_t *d, const json_value_t *args,
                     json_value_t *result, json_arena_t *arena) {
    (void)result; (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !args) return false;

    /* Find @cursor entity. */
    uint32_t cid = edit_entity_store_find_by_name(ctx->entities, "@cursor");
    if (cid == EDIT_ENTITY_INVALID_ID) return false;
    edit_entity_t *cur = edit_entity_store_get_mut(ctx->entities, cid);
    if (!cur) return false;

    float target[3] = {0, 0, 0};
    float target_rot[3] = {0, 0, 0};

    /* Check for entity_id argument. Treat empty string as absent. */
    const json_value_t *id_val = json_object_get(args, "entity_id");
    bool has_entity = id_val != NULL;
    if (has_entity && id_val->type == JSON_STRING && id_val->string.len == 0) {
        has_entity = false;
    }

    if (has_entity) {
        uint32_t eid = edit_cmd_resolve_entity(ctx, id_val);
        if (eid == EDIT_ENTITY_INVALID_ID) return false;
        const edit_entity_t *e = edit_entity_store_get(ctx->entities, eid);
        if (!e) return false;
        target[0] = e->pos[0];
        target[1] = e->pos[1];
        target[2] = e->pos[2];
        target_rot[0] = e->rot[0];
        target_rot[1] = e->rot[1];
        target_rot[2] = e->rot[2];
    } else {
        /* No entity specified — snap to selection centroid. */
        if (!ctx->selection) return false;
        uint32_t count = edit_selection_count(ctx->selection);
        if (count == 0) return false;
        const uint32_t *ids = edit_selection_ids(ctx->selection);

        for (uint32_t i = 0; i < count; i++) {
            const edit_entity_t *e = edit_entity_store_get(ctx->entities,
                                                            ids[i]);
            if (!e) continue;
            target[0] += e->pos[0];
            target[1] += e->pos[1];
            target[2] += e->pos[2];
        }
        target[0] /= (float)count;
        target[1] /= (float)count;
        target[2] /= (float)count;
    }

    /* Move cursor to target position and rotation. */
    cur->pos[0] = target[0];
    cur->pos[1] = target[1];
    cur->pos[2] = target[2];
    cur->rot[0] = target_rot[0];
    cur->rot[1] = target_rot[1];
    cur->rot[2] = target_rot[2];
    {
        static const float D2R = 3.14159265358979323846f / 180.0f;
        cur->orientation = quat_from_euler_yxz(
            target_rot[0] * D2R, target_rot[1] * D2R, target_rot[2] * D2R);
    }

    /* Notify bridge. */
    if (ctx->bridge && ctx->bridge->on_move) {
        ctx->bridge->on_move(ctx->bridge->user_data, cid,
                             cur->body_index, cur->pos);
    }

    return true;
}

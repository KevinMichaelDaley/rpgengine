/**
 * @file cmd_undo.c
 * @brief Undo command — reverses the last operation (or group).
 *
 * JSON args: {} (no arguments)
 * Response: sync-format {"entities":[...], "tombstones":[...], "full":false}
 * so the scene editor can process changes via its sync handler.
 *
 * Non-static functions (1 / 4 limit):
 *   cmd_undo
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_entity_json.h"
#include "ferrum/editor/edit_undo.h"
#include "ferrum/editor/undo_apply.h"
#include "ferrum/editor/json_parse.h"

#include <string.h>

/** @brief Maximum entities affected by a single undo step. */
#define UNDO_MAX_AFFECTED 64

/** @brief Key strings for the sync response object. */
static const char *K_ENTITIES   = "entities";
static const char *K_TOMBSTONES = "tombstones";
static const char *K_FULL       = "full";

/**
 * @brief Build a delta sync-format response with affected entities.
 */
static bool build_sync_response_(edit_cmd_ctx_t *ctx, json_value_t *result,
                                  json_arena_t *arena,
                                  const uint32_t *ids, const bool *deleted,
                                  uint32_t count) {
    uint32_t live = 0, tomb = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (deleted[i]) tomb++; else live++;
    }

    /* Check arena space (conservative estimate). */
    size_t needed = edit_entity_json_arena_bytes(live, live * 8);
    needed += (tomb + live + 10) * sizeof(json_value_t);
    needed += 3 * (sizeof(const char *) + sizeof(uint32_t) + sizeof(json_value_t));
    if (arena->used + needed > arena->cap) {
        result->type   = JSON_NUMBER;
        result->number = (double)count;
        return true;
    }

    /* Allocate entity items array. */
    json_value_t *ent_items = NULL;
    if (live > 0) {
        ent_items = (json_value_t *)(arena->buf + arena->used);
        arena->used += live * sizeof(json_value_t);
        memset(ent_items, 0, live * sizeof(json_value_t));
    }

    /* Allocate tombstone items array. */
    json_value_t *tomb_items = NULL;
    if (tomb > 0) {
        tomb_items = (json_value_t *)(arena->buf + arena->used);
        arena->used += tomb * sizeof(json_value_t);
        memset(tomb_items, 0, tomb * sizeof(json_value_t));
    }

    /* Fill arrays. */
    uint32_t ei = 0, ti = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (deleted[i]) {
            tomb_items[ti].type = JSON_NUMBER;
            tomb_items[ti].number = (double)ids[i];
            ti++;
        } else {
            const edit_entity_t *ent =
                edit_entity_store_get(ctx->entities, ids[i]);
            if (ent) {
                edit_entity_json_build(ent, ids[i], &ent_items[ei], arena);
                ei++;
            }
        }
    }

    /* Allocate object keys/key_lens/vals (3 entries). */
    const char **keys = (const char **)(arena->buf + arena->used);
    arena->used += 3 * sizeof(const char *);
    uint32_t *key_lens = (uint32_t *)(arena->buf + arena->used);
    arena->used += 3 * sizeof(uint32_t);
    json_value_t *vals = (json_value_t *)(arena->buf + arena->used);
    arena->used += 3 * sizeof(json_value_t);
    memset(vals, 0, 3 * sizeof(json_value_t));

    keys[0] = K_ENTITIES;   key_lens[0] = 8;
    keys[1] = K_TOMBSTONES; key_lens[1] = 10;
    keys[2] = K_FULL;       key_lens[2] = 4;

    vals[0].type = JSON_ARRAY;
    vals[0].array.items = ent_items;
    vals[0].array.count = ei;

    vals[1].type = JSON_ARRAY;
    vals[1].array.items = tomb_items;
    vals[1].array.count = ti;

    vals[2].type = JSON_BOOL;
    vals[2].boolean = false;

    result->type = JSON_OBJECT;
    result->object.keys = keys;
    result->object.key_lens = key_lens;
    result->object.vals = vals;
    result->object.count = 3;

    return true;
}

bool cmd_undo(edit_dispatch_t *d, const json_value_t *args,
              json_value_t *result, json_arena_t *arena) {
    (void)args;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->undo) return false;

    if (edit_undo_count(ctx->undo) == 0) {
        result->type   = JSON_NUMBER;
        result->number = 0.0;
        return true;
    }

    const edit_undo_entry_t *top = edit_undo_peek_undo(ctx->undo);
    if (!top) {
        result->type   = JSON_NUMBER;
        result->number = 0.0;
        return true;
    }

    uint32_t cursor_before = ctx->undo->cursor;
    uint32_t undone = edit_undo_step(ctx->undo);
    if (undone == 0) {
        result->type   = JSON_NUMBER;
        result->number = 0.0;
        return true;
    }

    /* Collect affected entity IDs. */
    uint32_t affected_ids[UNDO_MAX_AFFECTED];
    bool     affected_deleted[UNDO_MAX_AFFECTED];
    uint32_t affected_count = 0;

    /* Apply inverse operations in reverse order. */
    uint32_t cursor_after = ctx->undo->cursor;
    for (uint32_t i = cursor_before; i > cursor_after; i--) {
        uint32_t idx = (i - 1) % ctx->undo->capacity;
        const edit_undo_entry_t *entry = &ctx->undo->entries[idx];

        if (affected_count < UNDO_MAX_AFFECTED) {
            affected_ids[affected_count] = entry->entity_id;
            affected_deleted[affected_count] =
                (entry->inverse_type == EDIT_CMD_TYPE_DELETE);
            affected_count++;
        }

        edit_undo_apply_inverse(ctx, entry);
    }

    return build_sync_response_(ctx, result, arena,
                                 affected_ids, affected_deleted,
                                 affected_count);
}

/**
 * @file cmd_select_touching.c
 * @brief Select entities that collide with the current selection.
 *
 * select_touching: queries the bridge for collision results against each
 *   selected entity, then adds all touching entities to the selection.
 *   Skips @ entities (cursor, aliases). Returns count of newly selected.
 *
 * select_fill: repeats select_touching until the selection stops growing.
 *   This flood-fills through connected collision chains.
 *
 * JSON args: {} (both commands operate on current selection)
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"

#include <string.h>

/** Maximum touching results per entity query. */
#define MAX_TOUCHING_PER_ENTITY 256

/**
 * @brief One pass of select_touching: query each selected entity's neighbors
 *        and add them to the selection.
 *
 * @return Number of newly added entities.
 */
static uint32_t select_touching_pass_(edit_cmd_ctx_t *ctx) {
    uint32_t count = edit_selection_count(ctx->selection);
    if (count == 0) return 0;

    /* Snapshot current selection IDs (selection may grow during iteration). */
    const uint32_t *ids = edit_selection_ids(ctx->selection);
    uint32_t snapshot[EDIT_SELECTION_MAX];
    if (count > EDIT_SELECTION_MAX) count = EDIT_SELECTION_MAX;
    memcpy(snapshot, ids, count * sizeof(uint32_t));

    uint32_t newly_added = 0;
    uint32_t touching[MAX_TOUCHING_PER_ENTITY];

    for (uint32_t i = 0; i < count; i++) {
        uint32_t eid = snapshot[i];

        /* Query bridge for touching entities. */
        uint32_t n = ctx->bridge->on_query_touching(
            ctx->bridge->user_data, eid,
            touching, MAX_TOUCHING_PER_ENTITY);

        for (uint32_t j = 0; j < n; j++) {
            uint32_t tid = touching[j];

            /* Skip @ entities (cursor, aliases). */
            const edit_entity_t *te = edit_entity_store_get(ctx->entities,
                                                             tid);
            if (!te) continue;
            if (te->name[0] == '@') continue;

            /* Add to selection (no-op if already present). */
            if (!edit_selection_contains(ctx->selection, tid)) {
                edit_selection_add(ctx->selection, tid);
                newly_added++;
            }
        }
    }

    return newly_added;
}

bool cmd_select_touching(edit_dispatch_t *d, const json_value_t *args,
                         json_value_t *result, json_arena_t *arena) {
    (void)args; (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !ctx->selection) return false;

    /* Require bridge with on_query_touching callback. */
    if (!ctx->bridge || !ctx->bridge->on_query_touching) return false;

    uint32_t count = edit_selection_count(ctx->selection);
    if (count == 0) {
        result->type = JSON_NUMBER;
        result->number = 0;
        return true;
    }

    uint32_t added = select_touching_pass_(ctx);

    result->type = JSON_NUMBER;
    result->number = (double)added;
    return true;
}

bool cmd_select_fill(edit_dispatch_t *d, const json_value_t *args,
                     json_value_t *result, json_arena_t *arena) {
    (void)args; (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !ctx->selection) return false;

    /* Require bridge with on_query_touching callback. */
    if (!ctx->bridge || !ctx->bridge->on_query_touching) return false;

    uint32_t count = edit_selection_count(ctx->selection);
    if (count == 0) {
        result->type = JSON_NUMBER;
        result->number = 0;
        return true;
    }

    /* Iterate until selection stops growing. Cap at entity capacity to
     * prevent infinite loops from buggy bridge callbacks. */
    uint32_t total_added = 0;
    uint32_t max_iters = ctx->entities->capacity;
    for (uint32_t iter = 0; iter < max_iters; iter++) {
        uint32_t added = select_touching_pass_(ctx);
        total_added += added;
        if (added == 0) break;
    }

    result->type = JSON_NUMBER;
    result->number = (double)total_added;
    return true;
}

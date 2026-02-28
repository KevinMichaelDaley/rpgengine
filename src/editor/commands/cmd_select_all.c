/**
 * @file cmd_select_all.c
 * @brief Select-all and deselect-all commands.
 *
 * cmd_select_all:   Selects every active entity. Optional group_mask.
 * cmd_deselect_all: Clears the entire selection. Args: {} (none).
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"

bool cmd_select_all(edit_dispatch_t *d, const json_value_t *args,
                    json_value_t *result, json_arena_t *arena) {
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !ctx->selection) return false;

    /* Resolve optional group_mask. */
    bool mask_fail = false;
    const edit_group_t *mask = edit_cmd_resolve_group_mask(ctx, args,
                                                           &mask_fail);
    if (mask_fail) return false;

    if (mask) {
        /* Only select entities in the group. */
        for (uint32_t i = 0; i < mask->count; i++) {
            const edit_entity_t *e = edit_entity_store_get(ctx->entities,
                                                            mask->ids[i]);
            if (e) {
                edit_selection_add(ctx->selection, mask->ids[i]);
            }
        }
    } else {
        /* No mask — select all active entities. */
        uint32_t cap = ctx->entities->capacity;
        for (uint32_t i = 0; i < cap; i++) {
            const edit_entity_t *e = edit_entity_store_get(ctx->entities, i);
            if (e) {
                edit_selection_add(ctx->selection, i);
            }
        }
    }

    /* Return count selected. */
    result->type = JSON_NUMBER;
    result->number = (double)edit_selection_count(ctx->selection);
    return true;
}

bool cmd_deselect_all(edit_dispatch_t *d, const json_value_t *args,
                      json_value_t *result, json_arena_t *arena) {
    (void)args;
    (void)result;
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->selection) return false;

    edit_selection_clear(ctx->selection);
    return true;
}

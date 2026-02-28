/**
 * @file cmd_clone.c
 * @brief Clone command — duplicate selected entities.
 *
 * Duplicates all selected entities, applying an optional position offset.
 * Cloned entities become the new selection. Undo deletes all clones.
 *
 * JSON args: {"offset":[1,0,0]} (optional, defaults to [0,0,0])
 *
 * Non-static functions: 1 (cmd_clone).
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/edit_undo.h"

#include <string.h>

bool cmd_clone(edit_dispatch_t *d, const json_value_t *args,
               json_value_t *result, json_arena_t *arena) {
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !ctx->selection) return false;

    uint32_t sel_count = edit_selection_count(ctx->selection);
    if (sel_count == 0) return false;

    const uint32_t *sel_ids = edit_selection_ids(ctx->selection);

    /* Parse optional offset. */
    float offset[3] = {0.0f, 0.0f, 0.0f};
    if (args) {
        const json_value_t *ov = json_object_get(args, "offset");
        if (ov && ov->type == JSON_ARRAY && ov->array.count >= 3) {
            for (int i = 0; i < 3; i++) {
                if (ov->array.items[i].type == JSON_NUMBER)
                    offset[i] = (float)ov->array.items[i].number;
            }
        }
    }

    /* Clone each selected entity. Cap at reasonable limit. */
    uint32_t clone_ids[256];
    uint32_t clone_count = 0;
    uint32_t max_clones = sel_count < 256 ? sel_count : 256;

    for (uint32_t i = 0; i < max_clones; i++) {
        const edit_entity_t *src = edit_entity_store_get(ctx->entities,
                                                          sel_ids[i]);
        if (!src) continue;

        uint32_t new_id = edit_entity_store_create(ctx->entities, src->type);
        if (new_id == EDIT_ENTITY_INVALID_ID) break;

        edit_entity_t *dst = edit_entity_store_get_mut(ctx->entities, new_id);
        if (!dst) break;

        /* Deep copy all properties. */
        dst->pos[0] = src->pos[0] + offset[0];
        dst->pos[1] = src->pos[1] + offset[1];
        dst->pos[2] = src->pos[2] + offset[2];
        memcpy(dst->rot,   src->rot,   sizeof(src->rot));
        memcpy(dst->scale, src->scale, sizeof(src->scale));
        memcpy(dst->materials, src->materials, sizeof(src->materials));
        /* Name is NOT copied — clones get empty names by default. */

        /* Record undo: delete this clone to undo. */
        if (ctx->undo) {
            edit_undo_entry_t entry = {
                .forward_type = 0,
                .inverse_type = 0,
                .entity_id    = new_id,
            };
            edit_undo_record(ctx->undo, &entry, NULL, 0);
        }

        /* Notify physics bridge of new entity. */
        if (ctx->bridge && ctx->bridge->on_spawn) {
            uint32_t body = ctx->bridge->on_spawn(
                ctx->bridge->user_data, new_id, dst);
            dst->body_index = body;
        }

        clone_ids[clone_count++] = new_id;
    }

    if (clone_count == 0) return false;

    /* New selection = cloned entities only. */
    edit_selection_clear(ctx->selection);
    for (uint32_t i = 0; i < clone_count; i++) {
        edit_selection_add(ctx->selection, clone_ids[i]);
    }

    /* Return count of cloned entities. */
    result->type = JSON_NUMBER;
    result->number = (double)clone_count;
    return true;
}

/**
 * @file cmd_select.c
 * @brief Select/deselect commands — add or remove entities from selection by ID.
 *
 * cmd_select args:   {"entity_id": N, "toggle": true|false}
 *   - If toggle is true, toggles the entity in/out of selection.
 *   - If toggle is false (default), adds the entity to selection.
 *   - Returns {"selected": true|false} indicating final state.
 *   - Fails if entity_id does not exist.
 *
 * cmd_deselect args: {"entity_id": N}
 *   - Removes the entity from selection. No-op if not selected.
 *   - Fails if entity_id does not exist.
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"

#include <string.h>

bool cmd_select(edit_dispatch_t *d, const json_value_t *args,
                json_value_t *result, json_arena_t *arena) {
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !ctx->selection || !args) return false;

    /* Extract entity_id (required) — accepts number or name string. */
    const json_value_t *id_val = json_object_get(args, "entity_id");
    if (!id_val) return false;

    /* Check for & group prefix. */
    if (id_val->type == JSON_STRING && id_val->string.len > 0 &&
        id_val->string.ptr[0] == '&') {
        char name[EDIT_GROUP_NAME_MAX];
        uint32_t nlen = id_val->string.len;
        if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
        memcpy(name, id_val->string.ptr, nlen);
        name[nlen] = '\0';
        const edit_group_t *grp = edit_cmd_find_group(ctx, name);
        if (!grp) return false;
        for (uint32_t i = 0; i < grp->count; i++) {
            edit_selection_add(ctx->selection, grp->ids[i]);
        }
        result->type = JSON_NUMBER;
        result->number = (double)grp->count;
        return true;
    }

    uint32_t eid = edit_cmd_resolve_entity(ctx, id_val);
    if (eid == EDIT_ENTITY_INVALID_ID) return false;

    /* Validate entity exists. */
    const edit_entity_t *e = edit_entity_store_get(ctx->entities, eid);
    if (!e) return false;

    /* Check toggle flag. */
    bool toggle = false;
    const json_value_t *toggle_val = json_object_get(args, "toggle");
    if (toggle_val && toggle_val->type == JSON_BOOL && toggle_val->boolean) {
        toggle = true;
    }

    bool selected;
    if (toggle) {
        selected = edit_selection_toggle(ctx->selection, eid);
    } else {
        edit_selection_add(ctx->selection, eid);
        selected = true;
    }

    /* Return selection state. */
    result->type = JSON_BOOL;
    result->boolean = selected;
    return true;
}

bool cmd_deselect(edit_dispatch_t *d, const json_value_t *args,
                  json_value_t *result, json_arena_t *arena) {
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !ctx->selection || !args) return false;

    /* Extract entity_id (required) — accepts number or name string. */
    const json_value_t *id_val = json_object_get(args, "entity_id");
    if (!id_val) return false;

    /* Check for & group prefix. */
    if (id_val->type == JSON_STRING && id_val->string.len > 0 &&
        id_val->string.ptr[0] == '&') {
        char name[EDIT_GROUP_NAME_MAX];
        uint32_t nlen = id_val->string.len;
        if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
        memcpy(name, id_val->string.ptr, nlen);
        name[nlen] = '\0';
        const edit_group_t *grp = edit_cmd_find_group(ctx, name);
        if (!grp) return false;
        for (uint32_t i = 0; i < grp->count; i++) {
            edit_selection_remove(ctx->selection, grp->ids[i]);
        }
        result->type = JSON_NUMBER;
        result->number = (double)grp->count;
        return true;
    }

    uint32_t eid = edit_cmd_resolve_entity(ctx, id_val);
    if (eid == EDIT_ENTITY_INVALID_ID) return false;

    /* Validate entity exists. */
    const edit_entity_t *e = edit_entity_store_get(ctx->entities, eid);
    if (!e) return false;

    edit_selection_remove(ctx->selection, eid);
    return true;
}

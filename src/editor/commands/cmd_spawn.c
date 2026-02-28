/**
 * @file cmd_spawn.c
 * @brief Spawn command — creates a new entity at a given position.
 *
 * JSON args: {"type":"box"|"sphere", "pos":[x,y,z]}
 * Response result: entity_id (number)
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_undo.h"
#include <string.h>

/**
 * @brief Extract a 3-element float array from a JSON array value.
 */
static bool extract_vec3_(const json_value_t *arr, float out[3]) {
    if (!arr || arr->type != JSON_ARRAY || arr->array.count < 3) return false;
    for (int i = 0; i < 3; i++) {
        if (arr->array.items[i].type != JSON_NUMBER) return false;
        out[i] = (float)arr->array.items[i].number;
    }
    return true;
}

bool cmd_spawn(edit_dispatch_t *d, const json_value_t *args,
               json_value_t *result, json_arena_t *arena) {
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities) return false;

    /* Parse entity type from registry. */
    uint32_t type = EDIT_ENTITY_TYPE_BOX;
    if (args) {
        const json_value_t *type_val = json_object_get(args, "type");
        if (type_val && type_val->type == JSON_STRING) {
            /* Copy name to null-terminated buffer for lookup. */
            char name[32];
            uint32_t nlen = type_val->string.len;
            if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
            memcpy(name, type_val->string.ptr, nlen);
            name[nlen] = '\0';
            uint32_t resolved = edit_entity_type_by_name(name);
            if (resolved != UINT32_MAX) {
                type = resolved;
            }
        }
    }

    /* Create entity. */
    uint32_t eid = edit_entity_store_create(ctx->entities, type);
    if (eid == EDIT_ENTITY_INVALID_ID) return false;

    /* Set position if provided. */
    float pos[3] = {0};
    if (args) {
        const json_value_t *pos_val = json_object_get(args, "pos");
        if (extract_vec3_(pos_val, pos)) {
            edit_entity_t *e = edit_entity_store_get_mut(ctx->entities, eid);
            e->pos[0] = pos[0];
            e->pos[1] = pos[1];
            e->pos[2] = pos[2];
        }
    }

    /* Bridge: notify physics engine about the new entity. */
    if (ctx->bridge && ctx->bridge->on_spawn) {
        const edit_entity_t *ent = edit_entity_store_get(ctx->entities, eid);
        uint32_t body_idx = ctx->bridge->on_spawn(
            ctx->bridge->user_data, eid, ent);
        edit_entity_t *e = edit_entity_store_get_mut(ctx->entities, eid);
        if (e) e->body_index = body_idx;
    }

    /* Record undo entry. */
    if (ctx->undo) {
        edit_undo_entry_t entry = {0};
        entry.forward_type = EDIT_CMD_TYPE_SPAWN;
        entry.inverse_type = EDIT_CMD_TYPE_DELETE;
        entry.entity_id    = eid;
        edit_undo_record(ctx->undo, &entry, NULL, 0);
    }

    /* Return entity ID as result. */
    result->type   = JSON_NUMBER;
    result->number = (double)eid;
    return true;
}

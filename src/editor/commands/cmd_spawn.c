/**
 * @file cmd_spawn.c
 * @brief Spawn command — creates a new entity at a given position.
 *
 * JSON args: {"type":"box"|"sphere", "pos":[x,y,z], "rot":[rx,ry,rz], "scale":[sx,sy,sz]}
 * All args except type are optional.
 * Response result: entity_id (number) or name (string)
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_undo.h"
#include "ferrum/entity/entity_attrs.h"
#include "ferrum/math/quat.h"
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
            char tname[32];
            uint32_t nlen = type_val->string.len;
            if (nlen >= sizeof(tname)) nlen = sizeof(tname) - 1;
            memcpy(tname, type_val->string.ptr, nlen);
            tname[nlen] = '\0';
            uint32_t resolved = edit_entity_type_by_name(tname);
            if (resolved != UINT32_MAX) {
                type = resolved;
            }
        }
    }

    /* Create entity. */
    uint32_t eid = edit_entity_store_create(ctx->entities, type);
    if (eid == EDIT_ENTITY_INVALID_ID) return false;

    /* Set name if provided. */
    bool has_name = false;
    if (args) {
        const json_value_t *name_val = json_object_get(args, "name");
        if (name_val && name_val->type == JSON_STRING &&
            name_val->string.len > 0) {
            edit_entity_t *e = edit_entity_store_get_mut(ctx->entities, eid);
            uint32_t nlen = name_val->string.len;
            if (nlen >= EDIT_ENTITY_NAME_MAX) nlen = EDIT_ENTITY_NAME_MAX - 1;
            memcpy(e->name, name_val->string.ptr, nlen);
            e->name[nlen] = '\0';
            has_name = true;
        }
    }

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

    /* Set rotation if provided (Euler degrees). */
    if (args) {
        float rot[3] = {0};
        const json_value_t *rot_val = json_object_get(args, "rot");
        if (extract_vec3_(rot_val, rot)) {
            edit_entity_t *e = edit_entity_store_get_mut(ctx->entities, eid);
            e->rot[0] = rot[0];
            e->rot[1] = rot[1];
            e->rot[2] = rot[2];
            static const float D2R = 3.14159265358979323846f / 180.0f;
            e->orientation = quat_from_euler_yxz(
                rot[0] * D2R, rot[1] * D2R, rot[2] * D2R);
        }
    }

    /* Set scale if provided (per-axis factors). */
    if (args) {
        float scl[3] = {0};
        const json_value_t *scl_val = json_object_get(args, "scale");
        if (extract_vec3_(scl_val, scl)) {
            edit_entity_t *e = edit_entity_store_get_mut(ctx->entities, eid);
            e->scale[0] = scl[0];
            e->scale[1] = scl[1];
            e->scale[2] = scl[2];
        }
    }

    /* Set tier if provided (integer 0–3). */
    if (args) {
        const json_value_t *tier_val = json_object_get(args, "tier");
        if (tier_val && tier_val->type == JSON_NUMBER) {
            uint8_t tier = (uint8_t)tier_val->number;
            edit_entity_t *e = edit_entity_store_get_mut(ctx->entities, eid);
            entity_attrs_set(&e->attrs, SCRIPT_KEY_TIER,
                             SCRIPT_ATTR_BOOL, &tier, sizeof(tier));
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

    /* Return: name if provided, else entity ID. */
    if (has_name) {
        const edit_entity_t *ent = edit_entity_store_get(ctx->entities, eid);
        result->type       = JSON_STRING;
        result->string.ptr = ent->name;
        result->string.len = (uint32_t)strlen(ent->name);
    } else {
        result->type   = JSON_NUMBER;
        result->number = (double)eid;
    }
    return true;
}

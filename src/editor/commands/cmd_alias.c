/**
 * @file cmd_alias.c
 * @brief Alias commands: create and delete named reference points.
 *
 * Aliases are marker entities whose names start with '@'. They store
 * position and orientation and are skipped by spatial selection queries.
 *
 * alias_create: {"name":"@foo", "pos":[x,y,z], "rot":[rx,ry,rz]}
 *   - name must start with '@'. pos defaults to @cursor. rot defaults to 0.
 * alias_delete: {"name":"@foo"}
 *   - removes the alias entity.
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"

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

bool cmd_alias_create(edit_dispatch_t *d, const json_value_t *args,
                      json_value_t *result, json_arena_t *arena) {
    (void)result; (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !args) return false;

    /* Extract name (required, must start with @). */
    const json_value_t *name_val = json_object_get(args, "name");
    if (!name_val || name_val->type != JSON_STRING) return false;
    if (name_val->string.len == 0 || name_val->string.ptr[0] != '@') {
        return false;
    }

    /* Copy name with null termination. */
    char name[EDIT_ENTITY_NAME_MAX];
    uint32_t nlen = name_val->string.len;
    if (nlen >= EDIT_ENTITY_NAME_MAX) nlen = EDIT_ENTITY_NAME_MAX - 1;
    memcpy(name, name_val->string.ptr, nlen);
    name[nlen] = '\0';

    /* Extract position — optional, default to @cursor position. */
    float pos[3] = {0, 0, 0};
    const json_value_t *pos_val = json_object_get(args, "pos");
    if (pos_val && pos_val->type == JSON_ARRAY) {
        if (!extract_vec3_(pos_val, pos)) return false;
    } else {
        /* Default to @cursor position. */
        uint32_t cid = edit_entity_store_find_by_name(ctx->entities,
                                                       "@cursor");
        if (cid != EDIT_ENTITY_INVALID_ID) {
            const edit_entity_t *cur = edit_entity_store_get(ctx->entities,
                                                              cid);
            if (cur) {
                pos[0] = cur->pos[0];
                pos[1] = cur->pos[1];
                pos[2] = cur->pos[2];
            }
        }
    }

    /* Extract rotation — optional, default to identity. */
    float rot[3] = {0, 0, 0};
    const json_value_t *rot_val = json_object_get(args, "rot");
    if (rot_val && rot_val->type == JSON_ARRAY) {
        extract_vec3_(rot_val, rot);
    }

    /* Create marker entity. */
    uint32_t eid = edit_entity_store_create(ctx->entities,
                                             EDIT_ENTITY_TYPE_MARKER);
    if (eid == EDIT_ENTITY_INVALID_ID) return false;

    edit_entity_t *e = edit_entity_store_get_mut(ctx->entities, eid);
    if (!e) return false;

    e->pos[0] = pos[0]; e->pos[1] = pos[1]; e->pos[2] = pos[2];
    e->rot[0] = rot[0]; e->rot[1] = rot[1]; e->rot[2] = rot[2];
    memcpy(e->name, name, nlen + 1);

    return true;
}

bool cmd_alias_delete(edit_dispatch_t *d, const json_value_t *args,
                      json_value_t *result, json_arena_t *arena) {
    (void)result; (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !args) return false;

    /* Extract name (required). */
    const json_value_t *name_val = json_object_get(args, "name");
    if (!name_val || name_val->type != JSON_STRING) return false;

    char name[EDIT_ENTITY_NAME_MAX];
    uint32_t nlen = name_val->string.len;
    if (nlen >= EDIT_ENTITY_NAME_MAX) nlen = EDIT_ENTITY_NAME_MAX - 1;
    memcpy(name, name_val->string.ptr, nlen);
    name[nlen] = '\0';

    /* Find the alias. */
    uint32_t eid = edit_entity_store_find_by_name(ctx->entities, name);
    if (eid == EDIT_ENTITY_INVALID_ID) return false;

    return edit_entity_store_remove(ctx->entities, eid);
}

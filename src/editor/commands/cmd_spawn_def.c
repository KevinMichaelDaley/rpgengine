/**
 * @file cmd_spawn_def.c
 * @brief Editor command: spawn_def — spawn entity from .fentity definition.
 *
 * JSON args:
 *   {
 *     "path": "assets/entities/crate.fentity",
 *     "pos": [x, y, z],     (optional, overrides definition)
 *     "rot": [rx, ry, rz],  (optional, overrides definition)
 *     "name": "crate_01"     (optional, overrides definition)
 *   }
 *
 * Returns: entity ID (number).
 *
 * Non-static functions: 1 (cmd_spawn_def).
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_undo.h"
#include "ferrum/editor/edit_entity_version.h"
#include "ferrum/editor/def/entity_def.h"
#include "ferrum/entity/entity_attrs.h"

#include <math.h>
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

bool cmd_spawn_def(edit_dispatch_t *d, const json_value_t *args,
                   json_value_t *result, json_arena_t *arena) {
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !args) return false;

    /* Get definition path. */
    const json_value_t *path_val = json_object_get(args, "path");
    if (!path_val || path_val->type != JSON_STRING) return false;

    char path[256];
    uint32_t nlen = path_val->string.len;
    if (nlen >= sizeof(path)) nlen = sizeof(path) - 1;
    memcpy(path, path_val->string.ptr, nlen);
    path[nlen] = '\0';

    /* Load definition. */
    entity_def_t def;
    entity_def_result_t res = entity_def_load(path, &def);
    if (res != ENTITY_DEF_OK) {
        /* Return error string. */
        result->type = JSON_STRING;
        result->string.ptr = entity_def_result_str(res);
        result->string.len = (uint32_t)strlen(result->string.ptr);
        return false;
    }

    /* Determine entity type from definition. */
    uint32_t type = EDIT_ENTITY_TYPE_MESH;
    if (def.mesh_path[0] == '\0') {
        /* No mesh, check if named type. */
        if (strstr(def.name, "box") || strstr(def.name, "cube")) {
            type = EDIT_ENTITY_TYPE_BOX;
        } else if (strstr(def.name, "sphere")) {
            type = EDIT_ENTITY_TYPE_SPHERE;
        } else if (strstr(def.name, "capsule")) {
            type = EDIT_ENTITY_TYPE_CAPSULE;
        }
    }

    /* Create entity slot. */
    uint32_t eid = edit_entity_store_create(ctx->entities, type);
    if (eid == EDIT_ENTITY_INVALID_ID) return false;

    edit_entity_t *e = edit_entity_store_get_mut(ctx->entities, eid);
    if (!e) return false;

    /* Set name from definition (or override). */
    const json_value_t *name_val = json_object_get(args, "name");
    if (name_val && name_val->type == JSON_STRING && name_val->string.len > 0) {
        uint32_t nn = name_val->string.len;
        if (nn >= EDIT_ENTITY_NAME_MAX) nn = EDIT_ENTITY_NAME_MAX - 1;
        memcpy(e->name, name_val->string.ptr, nn);
        e->name[nn] = '\0';
    } else if (def.name[0] != '\0') {
        strncpy(e->name, def.name, EDIT_ENTITY_NAME_MAX - 1);
        e->name[EDIT_ENTITY_NAME_MAX - 1] = '\0';
    }

    /* Set position (override or default to origin). */
    float pos[3] = {0, 0, 0};
    if (extract_vec3_(json_object_get(args, "pos"), pos)) {
        e->pos[0] = pos[0]; e->pos[1] = pos[1]; e->pos[2] = pos[2];
    }

    /* Set rotation (override or default). */
    float rot[3] = {0, 0, 0};
    if (extract_vec3_(json_object_get(args, "rot"), rot)) {
        e->rot[0] = rot[0]; e->rot[1] = rot[1]; e->rot[2] = rot[2];
        static const float D2R = 3.14159265358979323846f / 180.0f;
        e->orientation = quat_from_euler_yxz(
            rot[0] * D2R, rot[1] * D2R, rot[2] * D2R);
    }

    /* Set scale to default. */
    e->scale[0] = 1.0f; e->scale[1] = 1.0f; e->scale[2] = 1.0f;

    /* Apply mesh path from definition. */
    if (def.mesh_path[0] != '\0') {
        entity_attrs_set(&e->attrs, SCRIPT_KEY_MESH_PATH, SCRIPT_ATTR_STR,
                         def.mesh_path, (uint8_t)(strlen(def.mesh_path) + 1));
    }

    /* Apply material path from definition. */
    if (def.material_path[0] != '\0') {
        /* Store in material slot 0. */
        strncpy(e->materials[0], def.material_path, EDIT_MATERIAL_PATH_MAX - 1);
        e->materials[0][EDIT_MATERIAL_PATH_MAX - 1] = '\0';
    }

    /* Apply physics properties. */
    if (def.is_static) {
        uint8_t b = 1;
        entity_attrs_set(&e->attrs, SCRIPT_KEY_STATIC, SCRIPT_ATTR_BOOL, &b, 1);
    }
    if (def.mass != 0.0f) {
        entity_attrs_set(&e->attrs, SCRIPT_KEY_MASS, SCRIPT_ATTR_F32,
                         &def.mass, 4);
    }
    if (def.friction != 0.5f) {
        entity_attrs_set(&e->attrs, SCRIPT_KEY_FRICTION, SCRIPT_ATTR_F32,
                         &def.friction, 4);
    }
    if (def.restitution != 0.0f) {
        entity_attrs_set(&e->attrs, SCRIPT_KEY_RESTITUTION, SCRIPT_ATTR_F32,
                         &def.restitution, 4);
    }

    /* Copy custom attributes from definition. */
    for (uint16_t i = 0; i < def.attrs.count; i++) {
        const attr_entry_t *entry = &def.attrs.entries[i];
        if (entry->key >= SCRIPT_KEY_USER) {
            /* Custom attribute, copy it. */
            const void *data = entity_attrs_get(&def.attrs, entry->key, NULL, NULL);
            if (data) {
                entity_attrs_set(&e->attrs, entry->key, entry->type, data, entry->size);
            }
        }
    }

    /* Bridge: notify physics engine. */
    if (ctx->bridge && ctx->bridge->on_spawn) {
        const edit_entity_t *ent = edit_entity_store_get(ctx->entities, eid);
        uint32_t body_idx = ctx->bridge->on_spawn(
            ctx->bridge->user_data, eid, ent);
        e = edit_entity_store_get_mut(ctx->entities, eid);
        if (e) e->body_index = body_idx;
    }

    /* Version stamp. */
    if (ctx->version) edit_version_stamp(ctx->version, eid);

    /* Record undo. */
    if (ctx->undo) {
        edit_undo_entry_t entry = {0};
        entry.forward_type = EDIT_CMD_TYPE_SPAWN;
        entry.inverse_type = EDIT_CMD_TYPE_DELETE;
        entry.entity_id    = eid;
        edit_undo_record(ctx->undo, &entry, NULL, 0);
    }

    /* Return entity ID. */
    result->type = JSON_NUMBER;
    result->number = (double)eid;
    return true;
}

/**
 * @file cmd_entity_def.c
 * @brief Editor command: entity_def — spawn entity with pre-applied attrs.
 *
 * Creates an entity and applies all attributes atomically before the bridge
 * callback fires, ensuring the physics engine sees the complete entity
 * definition (including trigger flags, custom attrs, etc.) on first contact.
 *
 * JSON args:
 *   {
 *     "name": "turret_0",          (optional)
 *     "type": "box",               (optional, default "box")
 *     "pos":  [x, y, z],           (optional)
 *     "rot":  [rx, ry, rz],        (optional)
 *     "scale":[sx, sy, sz],        (optional)
 *     "attrs": [                   (optional, array of {key, value} pairs)
 *       {"key": 256, "value": true},
 *       {"key": 257, "value": 42}
 *     ]
 *   }
 *
 * Returns: entity name (string) or entity id (number).
 *
 * Non-static functions: cmd_entity_def (1).
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_undo.h"
#include "ferrum/entity/entity_attrs.h"

#include <math.h>
#include <string.h>

/**
 * @brief Apply a single attribute from a JSON {key, value} pair.
 */
static bool apply_attr_(edit_entity_t *e, const json_value_t *entry) {
    if (!entry || entry->type != JSON_OBJECT) return false;

    const json_value_t *key_val = json_object_get(entry, "key");
    if (!key_val || key_val->type != JSON_NUMBER) return false;
    uint16_t key = (uint16_t)key_val->number;

    const json_value_t *val = json_object_get(entry, "value");
    if (!val) return false;

    switch (val->type) {
    case JSON_BOOL: {
        uint8_t b = val->boolean ? 1 : 0;
        return entity_attrs_set(&e->attrs, key, SCRIPT_ATTR_BOOL, &b, 1);
    }
    case JSON_NUMBER: {
        double num = val->number;
        if (num == floor(num) && num >= -2147483648.0 && num <= 2147483647.0) {
            int32_t i = (int32_t)num;
            return entity_attrs_set(&e->attrs, key, SCRIPT_ATTR_I32, &i, 4);
        }
        float f = (float)num;
        return entity_attrs_set(&e->attrs, key, SCRIPT_ATTR_F32, &f, 4);
    }
    case JSON_STRING: {
        uint8_t slen = (uint8_t)(val->string.len < 254 ? val->string.len : 254);
        char buf[256];
        memcpy(buf, val->string.ptr, slen);
        buf[slen] = '\0';
        return entity_attrs_set(&e->attrs, key, SCRIPT_ATTR_STR, buf,
                                (uint8_t)(slen + 1));
    }
    case JSON_ARRAY: {
        if (val->array.count == 3) {
            float v[3];
            for (int i = 0; i < 3; i++) {
                if (val->array.items[i].type != JSON_NUMBER) return false;
                v[i] = (float)val->array.items[i].number;
            }
            return entity_attrs_set(&e->attrs, key, SCRIPT_ATTR_VEC3, v, 12);
        }
        return false;
    }
    default:
        return false;
    }
}

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

bool cmd_entity_def(edit_dispatch_t *d, const json_value_t *args,
                    json_value_t *result, json_arena_t *arena) {
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities) return false;

    /* Parse entity type. */
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
            if (resolved != UINT32_MAX) type = resolved;
        }
    }

    /* Create entity slot. */
    uint32_t eid = edit_entity_store_create(ctx->entities, type);
    if (eid == EDIT_ENTITY_INVALID_ID) return false;

    edit_entity_t *e = edit_entity_store_get_mut(ctx->entities, eid);
    if (!e) return false;

    /* Set name. */
    bool has_name = false;
    if (args) {
        const json_value_t *name_val = json_object_get(args, "name");
        if (name_val && name_val->type == JSON_STRING &&
            name_val->string.len > 0) {
            uint32_t nlen = name_val->string.len;
            if (nlen >= EDIT_ENTITY_NAME_MAX) nlen = EDIT_ENTITY_NAME_MAX - 1;
            memcpy(e->name, name_val->string.ptr, nlen);
            e->name[nlen] = '\0';
            has_name = true;
        }
    }

    /* Set position. */
    if (args) {
        float pos[3];
        if (extract_vec3_(json_object_get(args, "pos"), pos)) {
            e->pos[0] = pos[0]; e->pos[1] = pos[1]; e->pos[2] = pos[2];
        }
    }

    /* Set rotation. */
    if (args) {
        float rot[3];
        if (extract_vec3_(json_object_get(args, "rot"), rot)) {
            e->rot[0] = rot[0]; e->rot[1] = rot[1]; e->rot[2] = rot[2];
        }
    }

    /* Set scale. */
    if (args) {
        float scl[3];
        if (extract_vec3_(json_object_get(args, "scale"), scl)) {
            e->scale[0] = scl[0]; e->scale[1] = scl[1]; e->scale[2] = scl[2];
        }
    }

    /* Apply attributes BEFORE the bridge callback fires. */
    if (args) {
        const json_value_t *attrs_val = json_object_get(args, "attrs");
        if (attrs_val && attrs_val->type == JSON_ARRAY) {
            for (uint32_t i = 0; i < attrs_val->array.count; i++) {
                apply_attr_(e, &attrs_val->array.items[i]);
            }
        }
    }

    /* Bridge: notify physics engine with fully-configured entity. */
    if (ctx->bridge && ctx->bridge->on_spawn) {
        const edit_entity_t *ent = edit_entity_store_get(ctx->entities, eid);
        uint32_t body_idx = ctx->bridge->on_spawn(
            ctx->bridge->user_data, eid, ent);
        e = edit_entity_store_get_mut(ctx->entities, eid);
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

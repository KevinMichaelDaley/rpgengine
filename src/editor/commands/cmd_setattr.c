/**
 * @file cmd_setattr.c
 * @brief Editor command: setattr — set an attribute on an entity.
 *
 * JSON args: {"entity": <id|name>, "key": <number>, "value": <val>}
 *
 * Value type is inferred from JSON type:
 *   - JSON_BOOL   → SCRIPT_ATTR_BOOL (1 byte)
 *   - JSON_NUMBER → SCRIPT_ATTR_F32  (4 bytes, float) if fractional,
 *                    SCRIPT_ATTR_I32  (4 bytes, int32) if integer
 *   - JSON_STRING → SCRIPT_ATTR_STR  (null-terminated)
 *   - JSON_ARRAY  → SCRIPT_ATTR_VEC3 (12 bytes) if 3 elements
 *
 * Returns: true on success.
 *
 * Non-static functions: cmd_setattr (1).
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_entity_version.h"
#include "ferrum/entity/entity_attrs.h"

#include <math.h>
#include <string.h>

bool cmd_setattr(edit_dispatch_t *d, const json_value_t *args,
                 json_value_t *result, json_arena_t *arena) {
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !args) return false;

    /* Resolve entity by id or name. */
    const json_value_t *ent_val = json_object_get(args, "entity");
    uint32_t eid = edit_cmd_resolve_entity(ctx, ent_val);
    if (eid == EDIT_ENTITY_INVALID_ID) return false;

    edit_entity_t *e = edit_entity_store_get_mut(ctx->entities, eid);
    if (!e || !e->active) return false;

    /* Extract key (numeric). */
    const json_value_t *key_val = json_object_get(args, "key");
    if (!key_val || key_val->type != JSON_NUMBER) return false;
    uint16_t key = (uint16_t)key_val->number;

    /* Extract and set value based on JSON type. */
    const json_value_t *val = json_object_get(args, "value");
    if (!val) return false;

    bool ok = false;

    switch (val->type) {
    case JSON_BOOL: {
        uint8_t b = val->boolean ? 1 : 0;
        ok = entity_attrs_set(&e->attrs, key, SCRIPT_ATTR_BOOL, &b, 1);
        break;
    }
    case JSON_NUMBER: {
        double num = val->number;
        if (num == floor(num) && num >= -2147483648.0 && num <= 2147483647.0) {
            int32_t i = (int32_t)num;
            ok = entity_attrs_set(&e->attrs, key, SCRIPT_ATTR_I32, &i, 4);
        } else {
            float f = (float)num;
            ok = entity_attrs_set(&e->attrs, key, SCRIPT_ATTR_F32, &f, 4);
        }
        break;
    }
    case JSON_STRING: {
        /* Include null terminator. */
        uint8_t slen = (uint8_t)(val->string.len < 254 ? val->string.len : 254);
        char buf[256];
        memcpy(buf, val->string.ptr, slen);
        buf[slen] = '\0';
        ok = entity_attrs_set(&e->attrs, key, SCRIPT_ATTR_STR, buf,
                              (uint8_t)(slen + 1));
        break;
    }
    case JSON_ARRAY: {
        if (val->array.count == 3) {
            float v[3];
            for (int i = 0; i < 3; i++) {
                if (val->array.items[i].type != JSON_NUMBER) return false;
                v[i] = (float)val->array.items[i].number;
            }
            ok = entity_attrs_set(&e->attrs, key, SCRIPT_ATTR_VEC3, v, 12);
        }
        break;
    }
    default:
        return false;
    }

    /* Version stamp the entity after attr change. */
    if (ok && ctx->version) edit_version_stamp(ctx->version, eid);

    result->type = JSON_BOOL;
    result->boolean = ok;
    return ok;
}

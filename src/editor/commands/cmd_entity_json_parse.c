/**
 * @file cmd_entity_json_parse.c
 * @brief Entity JSON deserialization — json_value_t → entity struct.
 *
 * Parses ALL edit_entity_t fields from a JSON object, including
 * dynamic attributes. Missing fields receive sensible defaults.
 *
 * Non-static functions (1 / 4 limit):
 *   edit_entity_json_parse
 */

#include "ferrum/editor/edit_entity_json.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/json_parse.h"
#include "ferrum/entity/entity_attrs.h"

#include <string.h>

/* ---- Static helpers ---- */

/** @brief Parse a vec3 JSON array into a float[3]. */
static void parse_vec3_(const json_value_t *arr, float out[3]) {
    if (!arr || arr->type != JSON_ARRAY) return;
    for (int i = 0; i < 3 && (uint32_t)i < arr->array.count; i++) {
        const json_value_t *elem = json_array_get(arr, (uint32_t)i);
        if (elem && elem->type == JSON_NUMBER) {
            out[i] = (float)elem->number;
        }
    }
}

/**
 * @brief Parse the "attrs" array of [key, type, value] triples into entity_attrs_t.
 */
static void parse_attrs_(const json_value_t *attrs_val, entity_attrs_t *attrs) {
    if (!attrs_val || attrs_val->type != JSON_ARRAY) return;

    for (uint32_t i = 0; i < attrs_val->array.count; i++) {
        const json_value_t *entry = &attrs_val->array.items[i];
        if (entry->type != JSON_ARRAY || entry->array.count < 3) continue;

        const json_value_t *key_v  = &entry->array.items[0];
        const json_value_t *type_v = &entry->array.items[1];
        const json_value_t *val_v  = &entry->array.items[2];

        if (key_v->type != JSON_NUMBER || type_v->type != JSON_NUMBER) continue;

        uint16_t key  = (uint16_t)key_v->number;
        uint8_t  type = (uint8_t)type_v->number;

        switch (type) {
        case SCRIPT_ATTR_F32: {
            if (val_v->type != JSON_NUMBER) break;
            float f = (float)val_v->number;
            entity_attrs_set(attrs, key, type, &f, sizeof(float));
            break;
        }
        case SCRIPT_ATTR_VEC3: {
            if (val_v->type != JSON_ARRAY || val_v->array.count < 3) break;
            float fv[3];
            for (int c = 0; c < 3; c++) {
                fv[c] = (float)val_v->array.items[c].number;
            }
            entity_attrs_set(attrs, key, type, fv, 12);
            break;
        }
        case SCRIPT_ATTR_I32: {
            if (val_v->type != JSON_NUMBER) break;
            int32_t iv = (int32_t)val_v->number;
            entity_attrs_set(attrs, key, type, &iv, sizeof(int32_t));
            break;
        }
        case SCRIPT_ATTR_U32: {
            if (val_v->type != JSON_NUMBER) break;
            uint32_t uv = (uint32_t)val_v->number;
            entity_attrs_set(attrs, key, type, &uv, sizeof(uint32_t));
            break;
        }
        case SCRIPT_ATTR_BOOL: {
            uint8_t bv;
            if (val_v->type == JSON_BOOL) {
                bv = val_v->boolean ? 1 : 0;
            } else if (val_v->type == JSON_NUMBER) {
                bv = (val_v->number != 0.0) ? 1 : 0;
            } else {
                break;
            }
            entity_attrs_set(attrs, key, type, &bv, sizeof(uint8_t));
            break;
        }
        case SCRIPT_ATTR_STR: {
            if (val_v->type != JSON_STRING) break;
            /* Build null-terminated copy. */
            char buf[256];
            uint32_t len = val_v->string.len;
            if (len >= sizeof(buf)) len = sizeof(buf) - 1;
            memcpy(buf, val_v->string.ptr, len);
            buf[len] = '\0';
            entity_attrs_set(attrs, key, type, buf, (uint8_t)(len + 1));
            break;
        }
        case SCRIPT_ATTR_BLOB: {
            if (val_v->type != JSON_ARRAY) break;
            uint8_t blob[255];
            uint32_t blen = val_v->array.count;
            if (blen > 255) blen = 255;
            for (uint32_t b = 0; b < blen; b++) {
                blob[b] = (uint8_t)val_v->array.items[b].number;
            }
            entity_attrs_set(attrs, key, type, blob, (uint8_t)blen);
            break;
        }
        default:
            break;
        }
    }
}

/* ---- Public API ---- */

void edit_entity_json_parse(const json_value_t *item,
                            edit_entity_t *snapshot) {
    if (!item || !snapshot) return;

    /* Zero and set defaults. */
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->active = true;
    snapshot->scale[0] = 1.0f;
    snapshot->scale[1] = 1.0f;
    snapshot->scale[2] = 1.0f;
    snapshot->orientation.w = 1.0f;
    snapshot->body_index = UINT32_MAX;
    entity_attrs_init(&snapshot->attrs);

    if (item->type != JSON_OBJECT) return;

    /* Type. */
    const json_value_t *type_val = json_object_get(item, "type");
    if (type_val && type_val->type == JSON_STRING) {
        char type_name[32];
        memset(type_name, 0, sizeof(type_name));
        json_string_copy(type_val, type_name, sizeof(type_name));
        uint32_t type_id = edit_entity_type_by_name(type_name);
        snapshot->type = (type_id != UINT32_MAX) ? type_id
                                                 : EDIT_ENTITY_TYPE_BOX;
    }

    /* Name. */
    const json_value_t *name_val = json_object_get(item, "name");
    if (name_val && name_val->type == JSON_STRING) {
        json_string_copy(name_val, snapshot->name, sizeof(snapshot->name));
    }

    /* Position. */
    parse_vec3_(json_object_get(item, "pos"), snapshot->pos);

    /* Orientation quaternion (xyzw). */
    const json_value_t *orient_val = json_object_get(item, "orient");
    if (orient_val && orient_val->type == JSON_ARRAY &&
        orient_val->array.count >= 4) {
        snapshot->orientation.x = (float)orient_val->array.items[0].number;
        snapshot->orientation.y = (float)orient_val->array.items[1].number;
        snapshot->orientation.z = (float)orient_val->array.items[2].number;
        snapshot->orientation.w = (float)orient_val->array.items[3].number;
    }

    /* Scale. */
    parse_vec3_(json_object_get(item, "scale"), snapshot->scale);

    /* Euler rotation cache. */
    parse_vec3_(json_object_get(item, "rot"), snapshot->rot);

    /* Pivot offset. */
    parse_vec3_(json_object_get(item, "pivot_offset"), snapshot->pivot_offset);

    /* Body index. */
    const json_value_t *bi_val = json_object_get(item, "body_index");
    if (bi_val && bi_val->type == JSON_NUMBER) {
        snapshot->body_index = (uint32_t)bi_val->number;
    }

    /* Hidden. */
    const json_value_t *hid_val = json_object_get(item, "hidden");
    if (hid_val && hid_val->type == JSON_BOOL) {
        snapshot->hidden = hid_val->boolean;
    }

    /* Pending delete. */
    const json_value_t *pd_val = json_object_get(item, "pending_delete");
    if (pd_val && pd_val->type == JSON_BOOL) {
        snapshot->pending_delete = pd_val->boolean;
    }

    /* Materials (array of strings, one per slot). */
    const json_value_t *mat_val = json_object_get(item, "materials");
    if (mat_val && mat_val->type == JSON_ARRAY) {
        uint32_t mat_count = mat_val->array.count;
        if (mat_count > EDIT_MATERIAL_SLOT_COUNT) {
            mat_count = EDIT_MATERIAL_SLOT_COUNT;
        }
        for (uint32_t s = 0; s < mat_count; s++) {
            const json_value_t *ms = &mat_val->array.items[s];
            if (ms->type == JSON_STRING && ms->string.len > 0) {
                uint32_t len = ms->string.len;
                if (len >= EDIT_MATERIAL_PATH_MAX) {
                    len = EDIT_MATERIAL_PATH_MAX - 1;
                }
                memcpy(snapshot->materials[s], ms->string.ptr, len);
                snapshot->materials[s][len] = '\0';
            }
        }
    }

    /* Dynamic attributes. */
    parse_attrs_(json_object_get(item, "attrs"), &snapshot->attrs);
}

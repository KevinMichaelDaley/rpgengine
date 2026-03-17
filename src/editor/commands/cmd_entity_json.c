/**
 * @file cmd_entity_json.c
 * @brief Entity JSON serialization — entity struct → json_value_t.
 *
 * Serializes ALL edit_entity_t fields into arena-allocated JSON objects
 * for server command responses (sync_entities, list_entities, etc.).
 *
 * Static fields serialized:
 *   id, name, type, pos, orient, scale, rot, pivot_offset,
 *   body_index, hidden, pending_delete, materials
 *
 * Dynamic attributes serialized as:
 *   "attrs": [[key, type, value], ...]
 *
 * Non-static functions (2 / 4 limit):
 *   edit_entity_json_arena_bytes
 *   edit_entity_json_build
 */

#include "ferrum/editor/edit_entity_json.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/json_parse.h"
#include "ferrum/entity/entity_attrs.h"

#include <string.h>

/* ---- Constants ---- */

/**
 * @brief Number of top-level keys in the serialized entity object.
 *
 * id, name, type, pos, orient, scale, rot, pivot_offset,
 * body_index, hidden, pending_delete, materials, attrs = 13
 */
#define ENT_KEY_COUNT 13

/** Number of vec3 sub-arrays: pos, scale, rot, pivot_offset. */
#define VEC3_COUNT 4

/** Alignment helper. */
#define ALIGN8(x) (((x) + 7u) & ~(size_t)7u)

/* ---- Static helpers ---- */

/** @brief Look up type name string from type ID. */
static const char *type_name_(uint32_t type_id) {
    uint32_t count = 0;
    const edit_entity_type_info_t *types = edit_entity_type_registry(&count);
    for (uint32_t i = 0; i < count; i++) {
        if (types[i].type_id == type_id) return types[i].name;
    }
    return "unknown";
}

/**
 * @brief Allocate aligned bytes from arena.
 * @return Pointer to allocated block, or NULL if insufficient space.
 */
static void *arena_alloc_(json_arena_t *arena, size_t bytes) {
    size_t aligned = ALIGN8(bytes);
    if (arena->used + aligned > arena->cap) return NULL;
    void *ptr = arena->buf + arena->used;
    arena->used += aligned;
    return ptr;
}

/**
 * @brief Fill a json_value_t as a vec3 array from float[3], using
 *        pre-allocated items.
 */
static void build_vec3_(json_value_t *out, json_value_t *items,
                        const float v[3]) {
    for (int c = 0; c < 3; c++) {
        items[c].type = JSON_NUMBER;
        items[c].number = (double)v[c];
    }
    out->type = JSON_ARRAY;
    out->array.items = items;
    out->array.count = 3;
}

/**
 * @brief Serialize dynamic attrs into a JSON array of [key, type, value] triples.
 *
 * For each attr entry:
 *   - F32:  [key, 0, number]
 *   - VEC3: [key, 1, [x, y, z]]
 *   - I32:  [key, 2, number]
 *   - U32:  [key, 3, number]
 *   - BOOL: [key, 4, bool]
 *   - STR:  [key, 5, "string"]
 *   - BLOB: [key, 6, [byte0, byte1, ...]]
 */
static bool build_attrs_(const entity_attrs_t *attrs, json_value_t *out,
                          json_arena_t *arena) {
    uint16_t count = attrs->count;

    if (count == 0) {
        out->type = JSON_ARRAY;
        out->array.items = NULL;
        out->array.count = 0;
        return true;
    }

    /* Outer array items (one per attr). */
    json_value_t *outer = (json_value_t *)arena_alloc_(
        arena, count * sizeof(json_value_t));
    if (!outer) return false;

    for (uint16_t i = 0; i < count; i++) {
        const attr_entry_t *e = &attrs->entries[i];

        /* Each triple: [key, type, value] = 3 items. */
        json_value_t *triple = (json_value_t *)arena_alloc_(
            arena, 3 * sizeof(json_value_t));
        if (!triple) return false;

        triple[0].type = JSON_NUMBER;
        triple[0].number = (double)e->key;

        triple[1].type = JSON_NUMBER;
        triple[1].number = (double)e->type;

        /* Value depends on type. */
        const uint8_t *payload = attrs->payload + e->offset;

        switch (e->type) {
        case SCRIPT_ATTR_F32: {
            float f;
            memcpy(&f, payload, sizeof(float));
            triple[2].type = JSON_NUMBER;
            triple[2].number = (double)f;
            break;
        }
        case SCRIPT_ATTR_VEC3: {
            json_value_t *v3 = (json_value_t *)arena_alloc_(
                arena, 3 * sizeof(json_value_t));
            if (!v3) return false;
            float fv[3];
            memcpy(fv, payload, sizeof(fv));
            for (int c = 0; c < 3; c++) {
                v3[c].type = JSON_NUMBER;
                v3[c].number = (double)fv[c];
            }
            triple[2].type = JSON_ARRAY;
            triple[2].array.items = v3;
            triple[2].array.count = 3;
            break;
        }
        case SCRIPT_ATTR_I32: {
            int32_t iv;
            memcpy(&iv, payload, sizeof(int32_t));
            triple[2].type = JSON_NUMBER;
            triple[2].number = (double)iv;
            break;
        }
        case SCRIPT_ATTR_U32: {
            uint32_t uv;
            memcpy(&uv, payload, sizeof(uint32_t));
            triple[2].type = JSON_NUMBER;
            triple[2].number = (double)uv;
            break;
        }
        case SCRIPT_ATTR_BOOL: {
            triple[2].type = JSON_BOOL;
            triple[2].boolean = (payload[0] != 0);
            break;
        }
        case SCRIPT_ATTR_STR: {
            triple[2].type = JSON_STRING;
            triple[2].string.ptr = (const char *)payload;
            /* String is null-terminated in payload; len excludes null. */
            triple[2].string.len = (e->size > 0) ? (uint32_t)(e->size - 1) : 0;
            break;
        }
        case SCRIPT_ATTR_BLOB: {
            /* Serialize as array of byte values. */
            json_value_t *bytes = NULL;
            if (e->size > 0) {
                bytes = (json_value_t *)arena_alloc_(
                    arena, e->size * sizeof(json_value_t));
                if (!bytes) return false;
                for (uint8_t b = 0; b < e->size; b++) {
                    bytes[b].type = JSON_NUMBER;
                    bytes[b].number = (double)payload[b];
                }
            }
            triple[2].type = JSON_ARRAY;
            triple[2].array.items = bytes;
            triple[2].array.count = e->size;
            break;
        }
        default:
            /* Unknown type — serialize as null. */
            triple[2].type = JSON_NULL;
            break;
        }

        outer[i].type = JSON_ARRAY;
        outer[i].array.items = triple;
        outer[i].array.count = 3;
    }

    out->type = JSON_ARRAY;
    out->array.items = outer;
    out->array.count = count;
    return true;
}

/* ---- Public API ---- */

size_t edit_entity_json_arena_bytes(uint32_t entity_count,
                                    uint32_t total_attr_count) {
    /* Per entity: keys + key_lens + vals for ENT_KEY_COUNT fields. */
    size_t per_entity =
        ALIGN8(ENT_KEY_COUNT * sizeof(const char *)) +       /* keys */
        ALIGN8(ENT_KEY_COUNT * sizeof(uint32_t)) +           /* key_lens */
        ALIGN8(ENT_KEY_COUNT * sizeof(json_value_t)) +       /* vals */
        ALIGN8(VEC3_COUNT * 3 * sizeof(json_value_t)) +      /* vec3 sub-arrays */
        ALIGN8(4 * sizeof(json_value_t)) +                   /* quat sub-array */
        ALIGN8(EDIT_MATERIAL_SLOT_COUNT * sizeof(json_value_t)); /* material items */

    /* Per dynamic attr: outer item + triple + potential vec3/blob sub-array. */
    size_t per_attr =
        ALIGN8(sizeof(json_value_t)) +           /* outer array item */
        ALIGN8(3 * sizeof(json_value_t)) +       /* triple */
        ALIGN8(3 * sizeof(json_value_t)) +       /* vec3 sub-array (worst case) */
        ALIGN8(255 * sizeof(json_value_t));       /* blob bytes (worst case) */

    /* Wrapper overhead for items array. */
    size_t items_overhead = ALIGN8(entity_count * sizeof(json_value_t));

    return items_overhead
         + (size_t)entity_count * per_entity
         + (size_t)total_attr_count * per_attr
         + 1024; /* safety margin */
}

bool edit_entity_json_build(const edit_entity_t *ent, uint32_t eid,
                            json_value_t *out, json_arena_t *arena) {
    /* Allocate keys, key_lens, vals. */
    const char **keys = (const char **)arena_alloc_(
        arena, ENT_KEY_COUNT * sizeof(const char *));
    uint32_t *klens = (uint32_t *)arena_alloc_(
        arena, ENT_KEY_COUNT * sizeof(uint32_t));
    json_value_t *vals = (json_value_t *)arena_alloc_(
        arena, ENT_KEY_COUNT * sizeof(json_value_t));
    if (!keys || !klens || !vals) return false;

    /* Vec3 sub-arrays: pos, scale, rot, pivot_offset (4 * 3 items). */
    json_value_t *v3_items = (json_value_t *)arena_alloc_(
        arena, VEC3_COUNT * 3 * sizeof(json_value_t));
    /* Quat sub-array: 4 items. */
    json_value_t *quat_items = (json_value_t *)arena_alloc_(
        arena, 4 * sizeof(json_value_t));
    /* Material string items. */
    json_value_t *mat_items = (json_value_t *)arena_alloc_(
        arena, EDIT_MATERIAL_SLOT_COUNT * sizeof(json_value_t));
    if (!v3_items || !quat_items || !mat_items) return false;

    /* Static key names and lengths. */
    static const char *key_strs[ENT_KEY_COUNT] = {
        "id", "name", "type", "pos", "orient", "scale",
        "rot", "pivot_offset", "body_index", "hidden",
        "pending_delete", "materials", "attrs"
    };
    static const uint32_t key_lens[ENT_KEY_COUNT] = {
        2, 4, 4, 3, 6, 5,
        3, 12, 10, 6,
        14, 9, 5
    };

    for (int k = 0; k < ENT_KEY_COUNT; k++) {
        keys[k] = key_strs[k];
        klens[k] = key_lens[k];
    }

    /* 0: id (number). */
    vals[0].type = JSON_NUMBER;
    vals[0].number = (double)eid;

    /* 1: name (string). */
    vals[1].type = JSON_STRING;
    vals[1].string.ptr = ent->name[0] ? ent->name : "";
    vals[1].string.len = (uint32_t)strlen(vals[1].string.ptr);

    /* 2: type (string). */
    const char *tname = type_name_(ent->type);
    vals[2].type = JSON_STRING;
    vals[2].string.ptr = tname;
    vals[2].string.len = (uint32_t)strlen(tname);

    /* 3: pos (vec3). */
    build_vec3_(&vals[3], &v3_items[0], ent->pos);

    /* 4: orient (quaternion xyzw). */
    quat_items[0].type = JSON_NUMBER; quat_items[0].number = (double)ent->orientation.x;
    quat_items[1].type = JSON_NUMBER; quat_items[1].number = (double)ent->orientation.y;
    quat_items[2].type = JSON_NUMBER; quat_items[2].number = (double)ent->orientation.z;
    quat_items[3].type = JSON_NUMBER; quat_items[3].number = (double)ent->orientation.w;
    vals[4].type = JSON_ARRAY;
    vals[4].array.items = quat_items;
    vals[4].array.count = 4;

    /* 5: scale (vec3). */
    build_vec3_(&vals[5], &v3_items[3], ent->scale);

    /* 6: rot (vec3, euler degrees). */
    build_vec3_(&vals[6], &v3_items[6], ent->rot);

    /* 7: pivot_offset (vec3). */
    build_vec3_(&vals[7], &v3_items[9], ent->pivot_offset);

    /* 8: body_index (number). */
    vals[8].type = JSON_NUMBER;
    vals[8].number = (double)ent->body_index;

    /* 9: hidden (bool). */
    vals[9].type = JSON_BOOL;
    vals[9].boolean = ent->hidden;

    /* 10: pending_delete (bool). */
    vals[10].type = JSON_BOOL;
    vals[10].boolean = ent->pending_delete;

    /* 11: materials (array of EDIT_MATERIAL_SLOT_COUNT strings). */
    for (int s = 0; s < EDIT_MATERIAL_SLOT_COUNT; s++) {
        mat_items[s].type = JSON_STRING;
        mat_items[s].string.ptr = ent->materials[s][0] ? ent->materials[s] : "";
        mat_items[s].string.len = (uint32_t)strlen(mat_items[s].string.ptr);
    }
    vals[11].type = JSON_ARRAY;
    vals[11].array.items = mat_items;
    vals[11].array.count = EDIT_MATERIAL_SLOT_COUNT;

    /* 12: attrs (dynamic attributes). */
    if (!build_attrs_(&ent->attrs, &vals[12], arena)) return false;

    /* Build the outer object. */
    out->type = JSON_OBJECT;
    out->object.keys = keys;
    out->object.key_lens = klens;
    out->object.vals = vals;
    out->object.count = ENT_KEY_COUNT;
    return true;
}

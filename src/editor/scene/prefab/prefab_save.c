/**
 * @file prefab_save.c
 * @brief Serialize prefab definitions to JSON and write to file.
 *
 * Builds a json_value_t tree from prefab_def_t and serializes
 * it to compact JSON. Entity snapshots include type, transform,
 * name, local_parent, and all attrs.
 *
 * Non-static functions: prefab_serialize, prefab_save (2/4).
 */

#include "ferrum/editor/scene/prefab/prefab_save.h"
#include "ferrum/editor/scene/prefab/prefab_def.h"
#include "ferrum/editor/json_parse.h"

#include <stdio.h>
#include <string.h>

/** Arena buffer for JSON tree building. */
#define SERIALIZE_ARENA_SIZE (512 * 1024)
static uint8_t s_arena_buf[SERIALIZE_ARENA_SIZE];

/* ---- Static helpers ---- */

static void *arena_alloc(json_arena_t *arena, size_t size, size_t align) {
    size_t offset = (arena->used + align - 1) & ~(align - 1);
    if (offset + size > arena->cap) return NULL;
    void *ptr = arena->buf + offset;
    arena->used = offset + size;
    return ptr;
}

static json_value_t make_number(double val) {
    json_value_t v;
    memset(&v, 0, sizeof(v));
    v.type = JSON_NUMBER;
    v.number = val;
    return v;
}

static json_value_t make_string(json_arena_t *arena, const char *str) {
    json_value_t v;
    memset(&v, 0, sizeof(v));
    v.type = JSON_STRING;
    uint32_t len = (uint32_t)strlen(str);
    char *copy = arena_alloc(arena, len, 1);
    if (copy) {
        memcpy(copy, str, len);
        v.string.ptr = copy;
        v.string.len = len;
    }
    return v;
}

static json_value_t make_float_array(json_arena_t *arena, const float *vals,
                                      uint32_t count) {
    json_value_t v;
    memset(&v, 0, sizeof(v));
    v.type = JSON_ARRAY;
    v.array.count = count;
    v.array.items = arena_alloc(arena, count * sizeof(json_value_t),
                                _Alignof(json_value_t));
    if (v.array.items) {
        for (uint32_t i = 0; i < count; i++) {
            v.array.items[i] = make_number((double)vals[i]);
        }
    }
    return v;
}

/**
 * @brief Build a JSON object for entity attrs.
 *
 * Serializes all key-value pairs from the attrs block.
 */
static json_value_t make_attrs_array(json_arena_t *arena,
                                      const entity_attrs_t *attrs) {
    json_value_t v;
    memset(&v, 0, sizeof(v));
    v.type = JSON_ARRAY;

    if (attrs->count == 0) {
        v.array.count = 0;
        v.array.items = NULL;
        return v;
    }

    /* Each attr becomes a small object: {key, type, data} */
    v.array.count = attrs->count;
    v.array.items = arena_alloc(arena, attrs->count * sizeof(json_value_t),
                                 _Alignof(json_value_t));
    if (!v.array.items) { v.array.count = 0; return v; }

    for (uint16_t i = 0; i < attrs->count; i++) {
        const attr_entry_t *entry = &attrs->entries[i];
        const uint8_t *payload = attrs->payload + entry->offset;

        /* Build attr object with 3 keys: key, type, value */
        json_value_t aobj;
        memset(&aobj, 0, sizeof(aobj));
        aobj.type = JSON_OBJECT;
        aobj.object.count = 3;
        aobj.object.keys = arena_alloc(arena, 3 * sizeof(const char *),
                                        _Alignof(const char *));
        aobj.object.key_lens = arena_alloc(arena, 3 * sizeof(uint32_t),
                                            _Alignof(uint32_t));
        aobj.object.vals = arena_alloc(arena, 3 * sizeof(json_value_t),
                                        _Alignof(json_value_t));

        if (!aobj.object.keys || !aobj.object.key_lens || !aobj.object.vals) {
            aobj.type = JSON_NULL;
            v.array.items[i] = aobj;
            continue;
        }

        static const char *akeys[] = {"k", "t", "v"};
        static const uint32_t aklens[] = {1, 1, 1};
        for (int k = 0; k < 3; k++) {
            aobj.object.keys[k] = akeys[k];
            aobj.object.key_lens[k] = aklens[k];
        }

        aobj.object.vals[0] = make_number((double)entry->key);
        aobj.object.vals[1] = make_number((double)entry->type);

        /* Serialize value based on type. */
        switch (entry->type) {
            case SCRIPT_ATTR_F32: {
                float fv; memcpy(&fv, payload, 4);
                aobj.object.vals[2] = make_number((double)fv);
                break;
            }
            case SCRIPT_ATTR_I32: {
                int32_t iv; memcpy(&iv, payload, 4);
                aobj.object.vals[2] = make_number((double)iv);
                break;
            }
            case SCRIPT_ATTR_U32: {
                uint32_t uv; memcpy(&uv, payload, 4);
                aobj.object.vals[2] = make_number((double)uv);
                break;
            }
            case SCRIPT_ATTR_BOOL: {
                aobj.object.vals[2] = make_number((double)payload[0]);
                break;
            }
            case SCRIPT_ATTR_VEC3: {
                aobj.object.vals[2] = make_float_array(arena,
                    (const float *)payload, 3);
                break;
            }
            case SCRIPT_ATTR_STR: {
                aobj.object.vals[2] = make_string(arena, (const char *)payload);
                break;
            }
            default: {
                /* Blob: encode as array of bytes. */
                json_value_t blob;
                memset(&blob, 0, sizeof(blob));
                blob.type = JSON_ARRAY;
                blob.array.count = entry->size;
                blob.array.items = arena_alloc(arena,
                    entry->size * sizeof(json_value_t),
                    _Alignof(json_value_t));
                if (blob.array.items) {
                    for (uint8_t b = 0; b < entry->size; b++) {
                        blob.array.items[b] = make_number((double)payload[b]);
                    }
                }
                aobj.object.vals[2] = blob;
                break;
            }
        }

        v.array.items[i] = aobj;
    }

    return v;
}

/**
 * @brief Build a JSON object for a single entity snapshot.
 */
static json_value_t make_entity_object(json_arena_t *arena,
                                        const prefab_entity_snapshot_t *snap) {
    /* 6 keys: type, pos, rot, scale, name, attrs */
    enum { NKEYS = 7 };
    json_value_t v;
    memset(&v, 0, sizeof(v));
    v.type = JSON_OBJECT;
    v.object.count = NKEYS;
    v.object.keys = arena_alloc(arena, NKEYS * sizeof(const char *),
                                 _Alignof(const char *));
    v.object.key_lens = arena_alloc(arena, NKEYS * sizeof(uint32_t),
                                     _Alignof(uint32_t));
    v.object.vals = arena_alloc(arena, NKEYS * sizeof(json_value_t),
                                 _Alignof(json_value_t));
    if (!v.object.keys || !v.object.key_lens || !v.object.vals) {
        v.type = JSON_NULL;
        return v;
    }

    static const char *ekeys[] = {
        "type", "pos", "rot", "scale", "name", "local_parent", "attrs"
    };
    static const uint32_t eklens[] = {4, 3, 3, 5, 4, 12, 5};

    for (int i = 0; i < NKEYS; i++) {
        v.object.keys[i] = ekeys[i];
        v.object.key_lens[i] = eklens[i];
    }

    v.object.vals[0] = make_number((double)snap->type);
    v.object.vals[1] = make_float_array(arena, snap->pos, 3);
    v.object.vals[2] = make_float_array(arena, snap->rot, 3);
    v.object.vals[3] = make_float_array(arena, snap->scale, 3);
    v.object.vals[4] = make_string(arena, snap->name);
    v.object.vals[5] = make_number((double)snap->local_parent);
    v.object.vals[6] = make_attrs_array(arena, &snap->attrs);

    return v;
}

static json_value_t make_bone_object(json_arena_t *arena,
                                      const prefab_def_t *def,
                                      uint32_t bone_idx) {
    enum { NKEYS = 7 };
    json_value_t v;
    memset(&v, 0, sizeof(v));
    v.type = JSON_OBJECT;
    v.object.count = NKEYS;
    v.object.keys = arena_alloc(arena, NKEYS * sizeof(const char *),
                                 _Alignof(const char *));
    v.object.key_lens = arena_alloc(arena, NKEYS * sizeof(uint32_t),
                                     _Alignof(uint32_t));
    v.object.vals = arena_alloc(arena, NKEYS * sizeof(json_value_t),
                                 _Alignof(json_value_t));
    if (!v.object.keys || !v.object.key_lens || !v.object.vals) {
        v.type = JSON_NULL;
        return v;
    }

    static const char *bkeys[] = {
        "shape_type", "params", "mass", "collision_group",
        "ccd", "hull_offset", "hull_count"
    };
    static const uint32_t bklens[] = {10, 6, 4, 15, 3, 11, 10};

    for (int i = 0; i < NKEYS; i++) {
        v.object.keys[i] = bkeys[i];
        v.object.key_lens[i] = bklens[i];
    }

    v.object.vals[0] = make_number((double)def->bones[bone_idx].shape_type);
    v.object.vals[1] = make_float_array(arena, def->bones[bone_idx].params, 6);
    v.object.vals[2] = make_number((double)def->bones[bone_idx].mass);
    v.object.vals[3] = make_number((double)def->bones[bone_idx].collision_group);
    v.object.vals[4] = make_number((double)def->bones[bone_idx].ccd_enabled);
    v.object.vals[5] = make_number((double)def->bones[bone_idx].hull_offset);
    v.object.vals[6] = make_number((double)def->bones[bone_idx].hull_count);

    return v;
}

/* ---- Public API ---- */

size_t prefab_serialize(const struct prefab_def *def, char *buf, size_t cap) {
    if (!def) return 0;

    json_arena_t arena;
    json_arena_init(&arena, s_arena_buf, SERIALIZE_ARENA_SIZE);

    /* Root object: {version, entities, bones, hull_verts, bone_poses} */
    enum { ROOT_KEYS = 5 };
    json_value_t root;
    memset(&root, 0, sizeof(root));
    root.type = JSON_OBJECT;
    root.object.count = ROOT_KEYS;
    root.object.keys = arena_alloc(&arena, ROOT_KEYS * sizeof(const char *),
                                    _Alignof(const char *));
    root.object.key_lens = arena_alloc(&arena, ROOT_KEYS * sizeof(uint32_t),
                                        _Alignof(uint32_t));
    root.object.vals = arena_alloc(&arena, ROOT_KEYS * sizeof(json_value_t),
                                    _Alignof(json_value_t));
    if (!root.object.keys || !root.object.key_lens || !root.object.vals) {
        return 0;
    }

    static const char *rkeys[] = {
        "version", "entities", "bones", "hull_verts", "bone_poses"
    };
    static const uint32_t rklens[] = {7, 8, 5, 10, 10};
    for (int i = 0; i < ROOT_KEYS; i++) {
        root.object.keys[i] = rkeys[i];
        root.object.key_lens[i] = rklens[i];
    }

    /* version */
    root.object.vals[0] = make_number((double)def->version);

    /* entities array */
    {
        json_value_t ents;
        memset(&ents, 0, sizeof(ents));
        ents.type = JSON_ARRAY;
        ents.array.count = def->entity_count;
        ents.array.items = arena_alloc(&arena,
            def->entity_count * sizeof(json_value_t),
            _Alignof(json_value_t));
        if (ents.array.items) {
            for (uint32_t e = 0; e < def->entity_count; e++) {
                ents.array.items[e] =
                    make_entity_object(&arena, &def->entities[e]);
            }
        }
        root.object.vals[1] = ents;
    }

    /* bones array */
    {
        json_value_t bones;
        memset(&bones, 0, sizeof(bones));
        bones.type = JSON_ARRAY;
        bones.array.count = def->bone_count;
        bones.array.items = arena_alloc(&arena,
            def->bone_count * sizeof(json_value_t),
            _Alignof(json_value_t));
        if (bones.array.items) {
            for (uint32_t b = 0; b < def->bone_count; b++) {
                bones.array.items[b] = make_bone_object(&arena, def, b);
            }
        }
        root.object.vals[2] = bones;
    }

    /* hull_verts */
    root.object.vals[3] = make_float_array(
        &arena, def->hull_verts, def->hull_vert_count * 3);

    /* bone_poses: array of 16-float arrays (rest_local matrices). */
    {
        json_value_t poses;
        memset(&poses, 0, sizeof(poses));
        poses.type = JSON_ARRAY;
        poses.array.count = def->bone_pose_count;
        poses.array.items = arena_alloc(&arena,
            def->bone_pose_count * sizeof(json_value_t),
            _Alignof(json_value_t));
        if (poses.array.items) {
            for (uint32_t bp = 0; bp < def->bone_pose_count; bp++) {
                poses.array.items[bp] = make_float_array(
                    &arena, def->bone_rest_local[bp], 16);
            }
        }
        root.object.vals[4] = poses;
    }

    return json_write(&root, buf, cap);
}

bool prefab_save(const char *path, const struct prefab_def *def) {
    if (!path || !def) return false;

    size_t needed = prefab_serialize(def, NULL, 0);
    if (needed == 0) return false;

    static char s_save_buf[512 * 1024];
    size_t buf_size = needed + 1;
    if (buf_size > sizeof(s_save_buf)) return false;

    size_t written = prefab_serialize(def, s_save_buf, buf_size);
    if (written == 0) return false;

    FILE *f = fopen(path, "w");
    if (!f) return false;

    size_t out = fwrite(s_save_buf, 1, written, f);
    fclose(f);

    return out == written;
}

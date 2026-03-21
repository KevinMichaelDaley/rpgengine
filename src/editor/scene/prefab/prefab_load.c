/**
 * @file prefab_load.c
 * @brief Deserialize prefab definitions from JSON.
 *
 * Parses .fpfab JSON text and populates a prefab_def_t with entity
 * snapshots and optional bone collider data.
 *
 * Non-static functions: prefab_deserialize, prefab_load (2/4).
 */

#include "ferrum/editor/scene/prefab/prefab_load.h"
#include "ferrum/editor/scene/prefab/prefab_def.h"
#include "ferrum/editor/json_parse.h"

#include <stdio.h>
#include <string.h>

/** Arena buffer for JSON parsing. */
#define PARSE_ARENA_SIZE (512 * 1024)
static uint8_t s_parse_arena_buf[PARSE_ARENA_SIZE];

/* ---- Static helpers ---- */

static double read_num(const json_value_t *val, double def) {
    if (!val || val->type != JSON_NUMBER) return def;
    return val->number;
}

/**
 * @brief Parse attrs array back into entity_attrs_t.
 */
static void parse_attrs(const json_value_t *arr, entity_attrs_t *attrs) {
    entity_attrs_init(attrs);
    if (!arr || arr->type != JSON_ARRAY) return;

    for (uint32_t i = 0; i < arr->array.count; i++) {
        const json_value_t *aobj = json_array_get(arr, i);
        if (!aobj || aobj->type != JSON_OBJECT) continue;

        uint16_t key = (uint16_t)read_num(json_object_get(aobj, "k"), 0);
        uint8_t type = (uint8_t)read_num(json_object_get(aobj, "t"), 0);
        const json_value_t *val = json_object_get(aobj, "v");
        if (!val) continue;

        switch (type) {
            case SCRIPT_ATTR_F32: {
                float fv = (float)read_num(val, 0);
                entity_attrs_set(attrs, key, type, &fv, sizeof(float));
                break;
            }
            case SCRIPT_ATTR_I32: {
                int32_t iv = (int32_t)read_num(val, 0);
                entity_attrs_set(attrs, key, type, &iv, sizeof(int32_t));
                break;
            }
            case SCRIPT_ATTR_U32: {
                uint32_t uv = (uint32_t)read_num(val, 0);
                entity_attrs_set(attrs, key, type, &uv, sizeof(uint32_t));
                break;
            }
            case SCRIPT_ATTR_BOOL: {
                uint8_t bv = (uint8_t)read_num(val, 0);
                entity_attrs_set(attrs, key, type, &bv, 1);
                break;
            }
            case SCRIPT_ATTR_VEC3: {
                float vec[3] = {0};
                if (val->type == JSON_ARRAY) {
                    for (int c = 0; c < 3 && (uint32_t)c < val->array.count; c++) {
                        vec[c] = (float)read_num(json_array_get(val, (uint32_t)c), 0);
                    }
                }
                entity_attrs_set(attrs, key, type, vec, 12);
                break;
            }
            case SCRIPT_ATTR_STR: {
                char sbuf[256];
                if (json_string_copy(val, sbuf, sizeof(sbuf))) {
                    entity_attrs_set(attrs, key, type, sbuf,
                                     (uint8_t)(strlen(sbuf) + 1));
                }
                break;
            }
            default: break; /* Skip blob for now. */
        }
    }
}

/**
 * @brief Parse a single entity snapshot from JSON.
 */
static void parse_entity(const json_value_t *obj,
                          prefab_entity_snapshot_t *snap) {
    memset(snap, 0, sizeof(*snap));
    snap->scale[0] = 1.0f;
    snap->scale[1] = 1.0f;
    snap->scale[2] = 1.0f;
    snap->local_parent = -1;
    entity_attrs_init(&snap->attrs);

    if (!obj || obj->type != JSON_OBJECT) return;

    snap->type = (uint32_t)read_num(json_object_get(obj, "type"), 0);
    snap->local_parent = (int32_t)read_num(
        json_object_get(obj, "local_parent"), -1);

    /* pos */
    const json_value_t *pos = json_object_get(obj, "pos");
    if (pos && pos->type == JSON_ARRAY) {
        for (int i = 0; i < 3 && (uint32_t)i < pos->array.count; i++) {
            snap->pos[i] = (float)read_num(json_array_get(pos, (uint32_t)i), 0);
        }
    }

    /* rot */
    const json_value_t *rot = json_object_get(obj, "rot");
    if (rot && rot->type == JSON_ARRAY) {
        for (int i = 0; i < 3 && (uint32_t)i < rot->array.count; i++) {
            snap->rot[i] = (float)read_num(json_array_get(rot, (uint32_t)i), 0);
        }
    }

    /* scale */
    const json_value_t *scl = json_object_get(obj, "scale");
    if (scl && scl->type == JSON_ARRAY) {
        for (int i = 0; i < 3 && (uint32_t)i < scl->array.count; i++) {
            snap->scale[i] = (float)read_num(json_array_get(scl, (uint32_t)i), 0);
        }
    }

    /* name */
    const json_value_t *name = json_object_get(obj, "name");
    if (name) {
        json_string_copy(name, snap->name, sizeof(snap->name));
    }

    /* attrs */
    parse_attrs(json_object_get(obj, "attrs"), &snap->attrs);
}

static void parse_bone(const json_value_t *obj, prefab_def_t *def,
                       uint32_t idx) {
    if (!obj || obj->type != JSON_OBJECT) return;

    def->bones[idx].shape_type = (uint32_t)read_num(
        json_object_get(obj, "shape_type"), 0);

    const json_value_t *params = json_object_get(obj, "params");
    if (params && params->type == JSON_ARRAY) {
        for (uint32_t i = 0; i < 6 && i < params->array.count; i++) {
            def->bones[idx].params[i] = (float)read_num(
                json_array_get(params, i), 0);
        }
    }

    def->bones[idx].mass = (float)read_num(
        json_object_get(obj, "mass"), 0);
    def->bones[idx].collision_group = (uint32_t)read_num(
        json_object_get(obj, "collision_group"), 0);
    def->bones[idx].ccd_enabled = (uint32_t)read_num(
        json_object_get(obj, "ccd"), 0);
    def->bones[idx].hull_offset = (uint32_t)read_num(
        json_object_get(obj, "hull_offset"), 0);
    def->bones[idx].hull_count = (uint32_t)read_num(
        json_object_get(obj, "hull_count"), 0);
}

/* ---- Public API ---- */

bool prefab_deserialize(const char *json, size_t len,
                        struct prefab_def *def) {
    if (!json || !def) return false;

    prefab_def_init(def);

    json_arena_t arena;
    json_arena_init(&arena, s_parse_arena_buf, PARSE_ARENA_SIZE);

    json_value_t root;
    if (!json_parse(json, len, &arena, &root)) return false;
    if (root.type != JSON_OBJECT) return false;

    /* Version check. */
    uint32_t version = (uint32_t)read_num(
        json_object_get(&root, "version"), 0);
    if (version != PREFAB_VERSION) return false;
    def->version = version;

    /* entities array */
    const json_value_t *ents = json_object_get(&root, "entities");
    if (ents && ents->type == JSON_ARRAY) {
        def->entity_count = ents->array.count;
        if (def->entity_count > PREFAB_MAX_ENTITIES) {
            def->entity_count = PREFAB_MAX_ENTITIES;
        }
        for (uint32_t i = 0; i < def->entity_count; i++) {
            parse_entity(json_array_get(ents, i), &def->entities[i]);
        }
    }

    /* bones array */
    const json_value_t *bones = json_object_get(&root, "bones");
    if (bones && bones->type == JSON_ARRAY) {
        def->bone_count = bones->array.count;
        if (def->bone_count > PREFAB_MAX_BONES) {
            def->bone_count = PREFAB_MAX_BONES;
        }
        for (uint32_t i = 0; i < def->bone_count; i++) {
            parse_bone(json_array_get(bones, i), def, i);
        }
    }

    /* hull_verts */
    const json_value_t *hverts = json_object_get(&root, "hull_verts");
    if (hverts && hverts->type == JSON_ARRAY) {
        uint32_t float_count = hverts->array.count;
        uint32_t vert_count = float_count / 3;
        if (vert_count > PREFAB_MAX_HULL_VERTS) {
            vert_count = PREFAB_MAX_HULL_VERTS;
            float_count = vert_count * 3;
        }
        def->hull_vert_count = vert_count;
        for (uint32_t i = 0; i < float_count; i++) {
            def->hull_verts[i] = (float)read_num(
                json_array_get(hverts, i), 0);
        }
    }

    /* bone_poses: per-bone rest_local overrides (optional). */
    const json_value_t *bposes = json_object_get(&root, "bone_poses");
    if (bposes && bposes->type == JSON_ARRAY) {
        def->bone_pose_count = bposes->array.count;
        if (def->bone_pose_count > PREFAB_MAX_BONES) {
            def->bone_pose_count = PREFAB_MAX_BONES;
        }
        for (uint32_t bp = 0; bp < def->bone_pose_count; bp++) {
            const json_value_t *mat = json_array_get(bposes, bp);
            if (mat && mat->type == JSON_ARRAY) {
                for (uint32_t f = 0; f < 16 && f < mat->array.count; f++) {
                    def->bone_rest_local[bp][f] = (float)read_num(
                        json_array_get(mat, f), 0);
                }
            }
        }
    }

    return true;
}

bool prefab_load(const char *path, struct prefab_def *def) {
    if (!path || !def) return false;

    FILE *f = fopen(path, "r");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0 || file_size > 4 * 1024 * 1024) {
        fclose(f);
        return false;
    }

    static char s_load_buf[512 * 1024];
    if ((size_t)file_size >= sizeof(s_load_buf)) {
        fclose(f);
        return false;
    }

    size_t read_bytes = fread(s_load_buf, 1, (size_t)file_size, f);
    fclose(f);

    if (read_bytes != (size_t)file_size) return false;

    return prefab_deserialize(s_load_buf, read_bytes, def);
}

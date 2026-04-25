/**
 * @file entity_def.c
 * @brief Entity definition file format implementation.
 *
 * Non-static functions (3):
 *   1. entity_def_init
 *   2. entity_def_load
 *   3. entity_def_parse
 */

#include "ferrum/editor/def/entity_def.h"
#include "ferrum/editor/json_parse.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Internal helpers                                                          */
/* ------------------------------------------------------------------------ */

/** Read entire file into malloc'd buffer. Returns NULL on failure. */
static char *read_file_(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0) {
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);

    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(buf, 1, (size_t)len, f);
    fclose(f);

    buf[read] = '\0';
    if (out_len) *out_len = read;
    return buf;
}

/** Copy string from json_value string into destination buffer. */
static void copy_string_(char *dst, size_t dst_size, const json_value_t *src) {
    if (!src || src->type != JSON_STRING || dst_size == 0) {
        if (dst_size > 0) dst[0] = '\0';
        return;
    }
    size_t len = src->string.len;
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, src->string.ptr, len);
    dst[len] = '\0';
}

/** Get object value by key, returns NULL if not found. */
static const json_value_t *object_get_(const json_value_t *obj, const char *key) {
    return json_object_get(obj, key);
}

/** Get number from object, returns default if not found. */
static double get_number_(const json_value_t *obj, const char *key, double def) {
    const json_value_t *v = object_get_(obj, key);
    if (!v || v->type != JSON_NUMBER) return def;
    return v->number;
}

/** Get bool from object, returns default if not found. */
static bool get_bool_(const json_value_t *obj, const char *key, bool def) {
    const json_value_t *v = object_get_(obj, key);
    if (!v || v->type != JSON_BOOL) return def;
    return v->boolean;
}

/** Parse attrs object into entity_attrs_t. */
static void parse_attrs_(entity_attrs_t *attrs, const json_value_t *obj) {
    if (!obj || obj->type != JSON_OBJECT) return;

    for (uint32_t i = 0; i < obj->object.count; i++) {
        const char *key_str = obj->object.keys[i];
        uint32_t key_len = obj->object.key_lens[i];
        const json_value_t *val = &obj->object.vals[i];

        /* Parse key as number or use hash for string keys. */
        uint16_t attr_key = 0;
        if (key_len > 0 && key_str[0] >= '0' && key_str[0] <= '9') {
            attr_key = (uint16_t)strtoul(key_str, NULL, 10);
        } else {
            /* String keys: hash to fit in uint16. */
            uint32_t hash = 0;
            for (uint32_t j = 0; j < key_len; j++) {
                hash = hash * 31 + (uint8_t)key_str[j];
            }
            attr_key = (uint16_t)(hash % 65536);
            /* Bias to user key range. */
            if (attr_key < SCRIPT_KEY_USER) attr_key += SCRIPT_KEY_USER;
        }

        /* Set attribute based on type. */
        switch (val->type) {
        case JSON_BOOL: {
            uint8_t b = val->boolean ? 1 : 0;
            entity_attrs_set(attrs, attr_key, SCRIPT_ATTR_BOOL, &b, 1);
            break;
        }
        case JSON_NUMBER: {
            double num = val->number;
            if (num == floor(num) && num >= -2147483648.0 && num <= 2147483647.0) {
                int32_t i = (int32_t)num;
                entity_attrs_set(attrs, attr_key, SCRIPT_ATTR_I32, &i, 4);
            } else {
                float f = (float)num;
                entity_attrs_set(attrs, attr_key, SCRIPT_ATTR_F32, &f, 4);
            }
            break;
        }
        case JSON_STRING: {
            char buf[256];
            uint8_t slen = (uint8_t)(val->string.len < 254 ? val->string.len : 254);
            memcpy(buf, val->string.ptr, slen);
            buf[slen] = '\0';
            entity_attrs_set(attrs, attr_key, SCRIPT_ATTR_STR, buf, (uint8_t)(slen + 1));
            break;
        }
        case JSON_ARRAY: {
            if (val->array.count == 3) {
                float v[3];
                bool valid = true;
                for (int j = 0; j < 3; j++) {
                    if (val->array.items[j].type != JSON_NUMBER) {
                        valid = false;
                        break;
                    }
                    v[j] = (float)val->array.items[j].number;
                }
                if (valid) {
                    entity_attrs_set(attrs, attr_key, SCRIPT_ATTR_VEC3, v, 12);
                }
            }
            break;
        }
        default:
            break;
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

void entity_def_init(entity_def_t *def) {
    if (!def) return;
    memset(def, 0, sizeof(*def));
    def->mass = 1.0f;
    def->friction = 0.5f;
    def->restitution = 0.0f;
    entity_attrs_init(&def->attrs);
}

entity_def_result_t entity_def_load(const char *path, entity_def_t *out_def) {
    if (!path || !out_def) return ENTITY_DEF_ERR_INVALID_SCHEMA;

    size_t len;
    char *json = read_file_(path, &len);
    if (!json) return ENTITY_DEF_ERR_FILE_NOT_FOUND;

    entity_def_result_t result = entity_def_parse(json, out_def);
    free(json);
    return result;
}

entity_def_result_t entity_def_parse(const char *json, entity_def_t *out_def) {
    if (!json || !out_def) return ENTITY_DEF_ERR_INVALID_SCHEMA;

    /* Allocate arena for JSON parse. */
    size_t arena_size = strlen(json) * 2 + 4096;
    uint8_t *arena_buf = (uint8_t *)malloc(arena_size);
    if (!arena_buf) return ENTITY_DEF_ERR_OUT_OF_MEMORY;

    json_arena_t arena = {arena_buf, arena_size, 0};
    json_value_t root;
    size_t json_len = strlen(json);
    if (!json_parse(json, json_len, &arena, &root)) {
        free(arena_buf);
        return ENTITY_DEF_ERR_PARSE_FAILED;
    }

    /* Initialize output with defaults. */
    entity_def_init(out_def);

    /* Must be an object. */
    if (root.type != JSON_OBJECT) {
        free(arena_buf);
        return ENTITY_DEF_ERR_INVALID_SCHEMA;
    }

    /* Parse name. */
    copy_string_(out_def->name, sizeof(out_def->name),
                 object_get_(&root, "name"));

    /* Parse mesh path. */
    copy_string_(out_def->mesh_path, sizeof(out_def->mesh_path),
                 object_get_(&root, "mesh"));

    /* Parse material path. */
    copy_string_(out_def->material_path, sizeof(out_def->material_path),
                 object_get_(&root, "material"));

    /* Parse scripts array. */
    const json_value_t *scripts = object_get_(&root, "scripts");
    if (scripts && scripts->type == JSON_ARRAY) {
        out_def->script_count = 0;
        for (uint32_t i = 0; i < scripts->array.count &&
             out_def->script_count < ENTITY_DEF_SCRIPTS_MAX; i++) {
            const json_value_t *s = &scripts->array.items[i];
            if (s->type == JSON_STRING) {
                copy_string_(out_def->scripts[out_def->script_count],
                            ENTITY_DEF_PATH_MAX, s);
                out_def->script_count++;
            }
        }
    }

    /* Parse physics object. */
    const json_value_t *physics = object_get_(&root, "physics");
    if (physics && physics->type == JSON_OBJECT) {
        out_def->is_static = get_bool_(physics, "static", false);
        out_def->is_kinematic = get_bool_(physics, "kinematic", false);
        out_def->mass = (float)get_number_(physics, "mass", 1.0);
        out_def->friction = (float)get_number_(physics, "friction", 0.5);
        out_def->restitution = (float)get_number_(physics, "restitution", 0.0);
    }

    /* Parse attrs object. */
    const json_value_t *attrs = object_get_(&root, "attrs");
    if (attrs && attrs->type == JSON_OBJECT) {
        parse_attrs_(&out_def->attrs, attrs);
    }

    free(arena_buf);
    return ENTITY_DEF_OK;
}

const char *entity_def_result_str(entity_def_result_t result) {
    switch (result) {
    case ENTITY_DEF_OK: return "OK";
    case ENTITY_DEF_ERR_FILE_NOT_FOUND: return "File not found";
    case ENTITY_DEF_ERR_PARSE_FAILED: return "JSON parse failed";
    case ENTITY_DEF_ERR_INVALID_SCHEMA: return "Invalid schema";
    case ENTITY_DEF_ERR_OUT_OF_MEMORY: return "Out of memory";
    default: return "Unknown error";
    }
}

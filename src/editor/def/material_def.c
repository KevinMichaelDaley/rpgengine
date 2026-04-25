/**
 * @file material_def.c
 * @brief Material definition file format implementation.
 *
 * Non-static functions (3):
 *   1. material_def_init
 *   2. material_def_load
 *   3. material_def_parse
 */

#include "ferrum/editor/def/material_def.h"
#include "ferrum/editor/json_parse.h"

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

/** Get number from object, returns default if not found. */
static double get_number_(const json_value_t *obj, const char *key, double def) {
    const json_value_t *v = json_object_get(obj, key);
    if (!v || v->type != JSON_NUMBER) return def;
    return v->number;
}

/** Get bool from object, returns default if not found. */
static bool get_bool_(const json_value_t *obj, const char *key, bool def) {
    const json_value_t *v = json_object_get(obj, key);
    if (!v || v->type != JSON_BOOL) return def;
    return v->boolean;
}

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

void material_def_init(material_def_t *def) {
    if (!def) return;
    memset(def, 0, sizeof(*def));
    def->roughness_factor = 1.0f;
    def->metallic_factor = 1.0f;
    def->emissive_strength = 1.0f;
    def->alpha_cutoff = 0.5f;
}

material_def_result_t material_def_load(const char *path, material_def_t *out_def) {
    if (!path || !out_def) return MATERIAL_DEF_ERR_INVALID_SCHEMA;

    size_t len;
    char *json = read_file_(path, &len);
    if (!json) return MATERIAL_DEF_ERR_FILE_NOT_FOUND;

    material_def_result_t result = material_def_parse(json, out_def);
    free(json);
    return result;
}

material_def_result_t material_def_parse(const char *json, material_def_t *out_def) {
    if (!json || !out_def) return MATERIAL_DEF_ERR_INVALID_SCHEMA;

    /* Allocate arena for JSON parse. */
    size_t arena_size = strlen(json) * 2 + 4096;
    uint8_t *arena_buf = (uint8_t *)malloc(arena_size);
    if (!arena_buf) return MATERIAL_DEF_ERR_OUT_OF_MEMORY;

    json_arena_t arena = {arena_buf, arena_size, 0};
    json_value_t root;
    size_t json_len = strlen(json);
    if (!json_parse(json, json_len, &arena, &root)) {
        free(arena_buf);
        return MATERIAL_DEF_ERR_PARSE_FAILED;
    }

    /* Initialize output with defaults. */
    material_def_init(out_def);

    /* Must be an object. */
    if (root.type != JSON_OBJECT) {
        free(arena_buf);
        return MATERIAL_DEF_ERR_INVALID_SCHEMA;
    }

    /* Parse name. */
    copy_string_(out_def->name, sizeof(out_def->name),
                 json_object_get(&root, "name"));

    /* Parse slots object. */
    const json_value_t *slots = json_object_get(&root, "slots");
    if (slots && slots->type == JSON_OBJECT) {
        copy_string_(out_def->slot_albedo, sizeof(out_def->slot_albedo),
                     json_object_get(slots, "albedo"));
        copy_string_(out_def->slot_normal, sizeof(out_def->slot_normal),
                     json_object_get(slots, "normal"));
        copy_string_(out_def->slot_roughness, sizeof(out_def->slot_roughness),
                     json_object_get(slots, "roughness"));
        copy_string_(out_def->slot_metallic, sizeof(out_def->slot_metallic),
                     json_object_get(slots, "metallic"));
        copy_string_(out_def->slot_emissive, sizeof(out_def->slot_emissive),
                     json_object_get(slots, "emissive"));
    }

    /* Parse params object. */
    const json_value_t *params = json_object_get(&root, "params");
    if (params && params->type == JSON_OBJECT) {
        out_def->roughness_factor = (float)get_number_(params, "roughness_factor", 1.0);
        out_def->metallic_factor = (float)get_number_(params, "metallic_factor", 1.0);
        out_def->emissive_strength = (float)get_number_(params, "emissive_strength", 1.0);
        out_def->alpha_cutoff = (float)get_number_(params, "alpha_cutoff", 0.5);
    }

    /* Parse flags object. */
    const json_value_t *flags = json_object_get(&root, "flags");
    if (flags && flags->type == JSON_OBJECT) {
        out_def->double_sided = get_bool_(flags, "double_sided", false);
        out_def->alpha_blend = get_bool_(flags, "alpha_blend", false);
        out_def->alpha_test = get_bool_(flags, "alpha_test", false);
    }

    free(arena_buf);
    return MATERIAL_DEF_OK;
}

const char *material_def_result_str(material_def_result_t result) {
    switch (result) {
    case MATERIAL_DEF_OK: return "OK";
    case MATERIAL_DEF_ERR_FILE_NOT_FOUND: return "File not found";
    case MATERIAL_DEF_ERR_PARSE_FAILED: return "JSON parse failed";
    case MATERIAL_DEF_ERR_INVALID_SCHEMA: return "Invalid schema";
    case MATERIAL_DEF_ERR_OUT_OF_MEMORY: return "Out of memory";
    default: return "Unknown error";
    }
}

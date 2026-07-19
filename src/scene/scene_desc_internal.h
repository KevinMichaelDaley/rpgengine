/**
 * @file scene_desc_internal.h
 * @brief Private helpers shared by the scene-descriptor parse translation units.
 *
 * Not a public API. Declares the per-section sub-parsers (each in its own .c to
 * respect the 4-function rule) and small JSON field accessors used across them.
 */
#ifndef FERRUM_SCENE_SCENE_DESC_INTERNAL_H
#define FERRUM_SCENE_SCENE_DESC_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

#include "ferrum/editor/json_parse.h"
#include "ferrum/scene/scene_desc.h"

struct arena;

/* ---- Per-section sub-parsers (root is the descriptor's top-level object) ---- */

/** Fill out->materials / material_count from the "materials" array (optional). */
bool scene_desc_parse_materials(const json_value_t *root, scene_desc_t *out);

/** Fill out->objects (arena-allocated) from the required "objects" array. */
bool scene_desc_parse_objects(const json_value_t *root, struct arena *arena,
                              scene_desc_t *out);

/** Fill out->lightdata from the optional "lightmap"/"sdf" sections. */
bool scene_desc_parse_lightdata(const json_value_t *root, scene_desc_t *out);

/** Fill out->probes from the optional "probes" section. */
bool scene_desc_parse_probes(const json_value_t *root, scene_desc_t *out);

/* ---- Small JSON field accessors (header-inline; not counted as module fns) ---- */

/** Copy a string field into a fixed buffer; empty string if absent/not-a-string. */
static inline void sd_field_str(const json_value_t *obj, const char *key,
                                char *buf, size_t cap)
{
    const json_value_t *v = obj ? json_object_get(obj, key) : NULL;
    if (v == NULL || v->type != JSON_STRING || !json_string_copy(v, buf, cap)) {
        if (cap > 0) buf[0] = '\0';
    }
}

/** Read a numeric field as float, or @p def if absent/not-a-number. */
static inline float sd_field_num(const json_value_t *obj, const char *key,
                                 float def)
{
    const json_value_t *v = obj ? json_object_get(obj, key) : NULL;
    return (v && v->type == JSON_NUMBER) ? (float)v->number : def;
}

/** Read a boolean field, or @p def if absent/not-a-bool. */
static inline bool sd_field_bool(const json_value_t *obj, const char *key,
                                 bool def)
{
    const json_value_t *v = obj ? json_object_get(obj, key) : NULL;
    return (v && v->type == JSON_BOOL) ? v->boolean : def;
}

/** Copy up to @p n floats from a numeric JSON array field; leaves @p out as-is
 *  for any missing element (so caller-prefilled defaults survive). */
static inline void sd_field_vec(const json_value_t *obj, const char *key,
                                float *out, unsigned n)
{
    const json_value_t *arr = obj ? json_object_get(obj, key) : NULL;
    if (arr == NULL || arr->type != JSON_ARRAY) return;
    for (unsigned i = 0; i < n && i < arr->array.count; ++i) {
        const json_value_t *e = json_array_get(arr, i);
        if (e && e->type == JSON_NUMBER) out[i] = (float)e->number;
    }
}

#endif /* FERRUM_SCENE_SCENE_DESC_INTERNAL_H */

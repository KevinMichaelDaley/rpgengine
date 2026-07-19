/**
 * @file scene_desc_parse_lights.c
 * @brief Parse the optional "lights" array: the level's discrete light set
 *        emitted by the exporter (rpg-8302). Absent = no lights. Arena-allocated
 *        like colliders/objects; degree spot cones are stored as cosines.
 */
#include <math.h>
#include <string.h>

#include "ferrum/memory/arena.h"
#include "scene_desc_internal.h"

/* Degrees -> radians (no <math.h> M_PI reliance under -Wpedantic). */
#define SD_DEG2RAD 0.017453292519943295f

/* Map a "kind" string to the light enum (default POINT for unknown/absent). */
static scene_desc_light_kind_t light_kind(const json_value_t *o)
{
    char s[16];
    const json_value_t *v = json_object_get(o, "kind");
    if (v == NULL || v->type != JSON_STRING || !json_string_copy(v, s, sizeof s))
        return SCENE_DESC_LIGHT_POINT;
    if (strcmp(s, "directional") == 0) return SCENE_DESC_LIGHT_DIRECTIONAL;
    if (strcmp(s, "spot") == 0)        return SCENE_DESC_LIGHT_SPOT;
    if (strcmp(s, "area") == 0)        return SCENE_DESC_LIGHT_AREA;
    return SCENE_DESC_LIGHT_POINT;
}

/* OR together the flag bits named in the "flags" string array (unknown names
 * ignored). Absent "flags" -> 0 (caller applies a per-kind default). */
static uint32_t light_flags(const json_value_t *o)
{
    const json_value_t *arr = json_object_get(o, "flags");
    if (arr == NULL || arr->type != JSON_ARRAY) return 0u;
    uint32_t f = 0u;
    for (uint32_t i = 0; i < arr->array.count; ++i) {
        const json_value_t *e = json_array_get(arr, i);
        char s[24];
        if (e == NULL || e->type != JSON_STRING || !json_string_copy(e, s, sizeof s))
            continue;
        if (strcmp(s, "realtime") == 0)         f |= SCENE_DESC_LIGHT_FLAG_REALTIME;
        else if (strcmp(s, "baked") == 0)       f |= SCENE_DESC_LIGHT_FLAG_BAKED;
        else if (strcmp(s, "shadow") == 0)      f |= SCENE_DESC_LIGHT_FLAG_SHADOW;
        else if (strcmp(s, "dynamic_indirect") == 0)
                                                f |= SCENE_DESC_LIGHT_FLAG_DYNAMIC_INDIRECT;
        else if (strcmp(s, "probe_gi") == 0)    f |= SCENE_DESC_LIGHT_FLAG_PROBE_GI;
        /* unknown flag name: ignored */
    }
    return f;
}

bool scene_desc_parse_lights(const json_value_t *root, struct arena *arena,
                             scene_desc_t *out)
{
    out->lights = NULL;
    out->light_count = 0;
    const json_value_t *arr = json_object_get(root, "lights");
    if (arr == NULL || arr->type != JSON_ARRAY) return true; /* optional */
    uint32_t n = arr->array.count;
    if (n == 0) return true;

    scene_desc_light_t *lights = arena_alloc(
        (arena_t *)arena, _Alignof(scene_desc_light_t),
        (size_t)n * sizeof *lights);
    if (lights == NULL) return false;   /* arena exhausted -> clean failure */
    memset(lights, 0, (size_t)n * sizeof *lights);

    for (uint32_t i = 0; i < n; ++i) {
        const json_value_t *o = json_array_get(arr, i);
        scene_desc_light_t *l = &lights[i];
        /* Engine defaults: white unit-intensity light, unbounded, identity cone. */
        l->color[0] = l->color[1] = l->color[2] = 1.0f;
        l->intensity = 1.0f;
        l->cos_inner = 1.0f;   /* cos(0deg)  */
        l->cos_outer = 0.0f;   /* cos(90deg) */
        if (o == NULL || o->type != JSON_OBJECT) continue; /* keep defaults */

        l->kind = light_kind(o);
        sd_field_str(o, "name", l->name, SCENE_DESC_OBJ_NAME_CAP);
        sd_field_vec(o, "position", l->position, 3);
        sd_field_vec(o, "direction", l->direction, 3);
        sd_field_vec(o, "color", l->color, 3);
        l->intensity = sd_field_num(o, "intensity", 1.0f);
        l->range = sd_field_num(o, "range", 0.0f);
        l->radius = sd_field_num(o, "radius", 0.0f);
        /* Exporter emits spot cones in degrees; store cosines to match the
         * renderer's render_light_t (cos_inner >= cos_outer for inner<outer). */
        float inner_deg = sd_field_num(o, "cone_inner_deg", 0.0f);
        float outer_deg = sd_field_num(o, "cone_outer_deg", 90.0f);
        l->cos_inner = cosf(inner_deg * SD_DEG2RAD);
        l->cos_outer = cosf(outer_deg * SD_DEG2RAD);

        l->flags = light_flags(o);
        /* A light with no explicit flags still lights the realtime pass. */
        if (l->flags == 0u) l->flags = SCENE_DESC_LIGHT_FLAG_REALTIME;
    }
    out->lights = lights;
    out->light_count = n;
    return true;
}

/**
 * @file world_desc_parse.c
 * @brief Parse + load a world descriptor: a set of irregular zones, each with its
 *        own scene + optional per-zone render config (rpg-da8c / rpg-yrnu).
 */
#include <stdio.h>
#include <string.h>

#include "ferrum/memory/arena.h"
#include "ferrum/scene/world_desc.h"
#include "scene_desc_internal.h"   /* sd_field_str / _vec + json_parse */

/* Parse one zone object. Requires "min","max" (3-arrays) + a non-empty "scene".
 * Returns false if the element is malformed. */
static bool parse_zone(const json_value_t *z, world_zone_t *out)
{
    if (z == NULL || z->type != JSON_OBJECT) return false;
    memset(out, 0, sizeof *out);
    sd_field_str(z, "name", out->name, sizeof out->name);

    const json_value_t *mn = json_object_get(z, "min");
    const json_value_t *mx = json_object_get(z, "max");
    if (mn == NULL || mn->type != JSON_ARRAY || mn->array.count < 3 ||
        mx == NULL || mx->type != JSON_ARRAY || mx->array.count < 3)
        return false;
    sd_field_vec(z, "min", out->box_min, 3);
    sd_field_vec(z, "max", out->box_max, 3);

    sd_field_str(z, "scene", out->scene, sizeof out->scene);
    if (out->scene[0] == '\0') return false;               /* scene is required. */
    sd_field_str(z, "render_config", out->render_config, sizeof out->render_config);
    return true;
}

bool world_desc_parse(const char *json, size_t len, struct arena *arena,
                      world_desc_t *out)
{
    if (json == NULL || arena == NULL || out == NULL) return false;
    memset(out, 0, sizeof *out);

    size_t jsize = len * 12u + 4096u;
    void *jbuf = arena_alloc((arena_t *)arena, 16u, jsize);
    if (jbuf == NULL) return false;
    json_arena_t ja;
    json_arena_init(&ja, jbuf, jsize);
    json_value_t root;
    if (!json_parse(json, len, &ja, &root) || root.type != JSON_OBJECT) return false;

    sd_field_str(&root, "name", out->name, sizeof out->name);
    sd_field_str(&root, "render_config", out->default_render_config,
                 sizeof out->default_render_config);

    const json_value_t *zarr = json_object_get(&root, "zones");
    if (zarr == NULL || zarr->type != JSON_ARRAY || zarr->array.count == 0)
        return false;                                       /* a world needs >=1 zone. */
    uint32_t n = zarr->array.count;
    world_zone_t *zones = arena_alloc((arena_t *)arena, 16u, (size_t)n * sizeof *zones);
    if (zones == NULL) return false;
    for (uint32_t i = 0; i < n; ++i) {
        if (!parse_zone(json_array_get(zarr, i), &zones[i])) return false;
    }
    out->zones = zones;
    out->zone_count = n;
    return true;
}

bool world_desc_load(const char *path, struct arena *arena, world_desc_t *out)
{
    if (path == NULL || arena == NULL || out == NULL) return false;
    FILE *f = fopen(path, "rb");
    if (f == NULL) return false;
    bool ok = false;
    if (fseek(f, 0, SEEK_END) == 0) {
        long sz = ftell(f);
        if (sz >= 0 && fseek(f, 0, SEEK_SET) == 0) {
            char *buf = arena_alloc((arena_t *)arena, 1u, (size_t)sz + 1u);
            if (buf != NULL && fread(buf, 1, (size_t)sz, f) == (size_t)sz) {
                buf[sz] = '\0';
                ok = world_desc_parse(buf, (size_t)sz, arena, out);
            }
        }
    }
    fclose(f);
    return ok;
}

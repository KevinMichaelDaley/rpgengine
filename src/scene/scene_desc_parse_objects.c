/**
 * @file scene_desc_parse_objects.c
 * @brief Parse the "materials" table and the ordered "objects" array of a
 *        scene descriptor (rpg-51nf). Object order is preserved verbatim because
 *        it is the lightmap bake order.
 */
#include <string.h>

#include "ferrum/memory/arena.h"
#include "scene_desc_internal.h"

/* Resolve a material name to its index in the descriptor table (-1 if absent). */
static int32_t material_index(const scene_desc_t *out, const char *name)
{
    for (uint32_t i = 0; i < out->material_count; ++i) {
        if (strcmp(out->materials[i], name) == 0) return (int32_t)i;
    }
    return -1;
}

bool scene_desc_parse_materials(const json_value_t *root, scene_desc_t *out)
{
    out->material_count = 0;
    const json_value_t *arr = json_object_get(root, "materials");
    if (arr == NULL || arr->type != JSON_ARRAY) return true; /* optional */
    uint32_t n = arr->array.count;
    if (n > SCENE_DESC_MAX_MATERIALS) n = SCENE_DESC_MAX_MATERIALS;
    for (uint32_t i = 0; i < n; ++i) {
        const json_value_t *e = json_array_get(arr, i);
        char *slot = out->materials[out->material_count];
        if (e == NULL || e->type != JSON_STRING ||
            !json_string_copy(e, slot, SCENE_DESC_MAT_NAME_CAP)) {
            slot[0] = '\0';
        }
        out->material_count++;
    }
    return true;
}

bool scene_desc_parse_objects(const json_value_t *root, struct arena *arena,
                              scene_desc_t *out)
{
    out->objects = NULL;
    out->object_count = 0;
    const json_value_t *arr = json_object_get(root, "objects");
    if (arr == NULL || arr->type != JSON_ARRAY) return false; /* required */
    uint32_t n = arr->array.count;
    if (n == 0) return true;

    scene_desc_object_t *objs = arena_alloc(
        (arena_t *)arena, _Alignof(scene_desc_object_t),
        (size_t)n * sizeof *objs);
    if (objs == NULL) return false;   /* arena exhausted -> clean failure */
    memset(objs, 0, (size_t)n * sizeof *objs);

    for (uint32_t i = 0; i < n; ++i) {
        const json_value_t *o = json_array_get(arr, i);
        scene_desc_object_t *d = &objs[i];
        /* Defaults: identity transform, no bake offset. */
        d->scale[0] = d->scale[1] = d->scale[2] = 1.0f;
        d->rotation[3] = 1.0f;
        d->sh_layer = 0;
        if (o == NULL || o->type != JSON_OBJECT) continue; /* keep defaults */

        sd_field_str(o, "name", d->name, SCENE_DESC_OBJ_NAME_CAP);
        sd_field_str(o, "mesh", d->mesh, SCENE_DESC_PATH_CAP);
        sd_field_str(o, "skeleton", d->skeleton, SCENE_DESC_PATH_CAP);
        sd_field_vec(o, "position", d->position, 3);
        sd_field_vec(o, "rotation", d->rotation, 4);
        sd_field_vec(o, "scale", d->scale, 3);
        d->lightmap_res = (int32_t)sd_field_num(o, "lightmap_res", 0.0f);
        d->sh_layer = (int32_t)sd_field_num(o, "sh_layer", 0.0f);

        const json_value_t *mats = json_object_get(o, "materials");
        if (mats != NULL && mats->type == JSON_ARRAY) {
            uint32_t mn = mats->array.count;
            if (mn > SCENE_DESC_MAX_OBJ_MATERIALS) mn = SCENE_DESC_MAX_OBJ_MATERIALS;
            for (uint32_t m = 0; m < mn; ++m) {
                const json_value_t *me = json_array_get(mats, m);
                char nm[SCENE_DESC_MAT_NAME_CAP];
                if (me != NULL && me->type == JSON_STRING &&
                    json_string_copy(me, nm, sizeof nm)) {
                    d->material_idx[d->material_count] = material_index(out, nm);
                } else {
                    d->material_idx[d->material_count] = -1;
                }
                d->material_count++;
            }
        }
    }
    out->objects = objs;
    out->object_count = n;
    return true;
}

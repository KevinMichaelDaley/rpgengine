/**
 * @file scene_desc_parse_colliders.c
 * @brief Parse the optional "colliders" array: the level's physics collision set
 *        (rpg-51nf). Absent = no colliders. Arena-allocated like objects.
 */
#include <string.h>

#include "ferrum/memory/arena.h"
#include "scene_desc_internal.h"

/* Map a "kind" string to the collider enum (default BOX for unknown/absent). */
static scene_desc_collider_kind_t collider_kind(const json_value_t *o)
{
    char s[16];
    const json_value_t *v = json_object_get(o, "kind");
    if (v == NULL || v->type != JSON_STRING || !json_string_copy(v, s, sizeof s))
        return SCENE_DESC_COLLIDER_BOX;
    if (strcmp(s, "sphere") == 0)    return SCENE_DESC_COLLIDER_SPHERE;
    if (strcmp(s, "capsule") == 0)   return SCENE_DESC_COLLIDER_CAPSULE;
    if (strcmp(s, "halfspace") == 0) return SCENE_DESC_COLLIDER_HALFSPACE;
    if (strcmp(s, "mesh") == 0)      return SCENE_DESC_COLLIDER_MESH;
    return SCENE_DESC_COLLIDER_BOX;
}

bool scene_desc_parse_colliders(const json_value_t *root, struct arena *arena,
                                scene_desc_t *out)
{
    out->colliders = NULL;
    out->collider_count = 0;
    const json_value_t *arr = json_object_get(root, "colliders");
    if (arr == NULL || arr->type != JSON_ARRAY) return true; /* optional */
    uint32_t n = arr->array.count;
    if (n == 0) return true;

    scene_desc_collider_t *cols = arena_alloc(
        (arena_t *)arena, _Alignof(scene_desc_collider_t),
        (size_t)n * sizeof *cols);
    if (cols == NULL) return false;   /* arena exhausted -> clean failure */
    memset(cols, 0, (size_t)n * sizeof *cols);

    for (uint32_t i = 0; i < n; ++i) {
        const json_value_t *o = json_array_get(arr, i);
        scene_desc_collider_t *c = &cols[i];
        /* Defaults: identity orientation, standalone, static (level geo). */
        c->rotation[3] = 1.0f;
        c->object_ref = -1;
        c->is_static = true;
        if (o == NULL || o->type != JSON_OBJECT) continue; /* keep defaults */

        c->kind = collider_kind(o);
        sd_field_str(o, "name", c->name, SCENE_DESC_OBJ_NAME_CAP);
        sd_field_vec(o, "position", c->position, 3);
        sd_field_vec(o, "rotation", c->rotation, 4);
        sd_field_vec(o, "half_extents", c->half_extents, 3);
        c->radius = sd_field_num(o, "radius", 0.0f);
        c->half_height = sd_field_num(o, "half_height", 0.0f);
        sd_field_vec(o, "normal", c->normal, 3);
        c->plane_offset = sd_field_num(o, "plane_offset", 0.0f);
        sd_field_str(o, "mesh", c->mesh, SCENE_DESC_PATH_CAP);
        c->object_ref = (int32_t)sd_field_num(o, "object_ref", -1.0f);
        c->is_static = sd_field_bool(o, "static", true);
    }
    out->colliders = cols;
    out->collider_count = n;
    return true;
}

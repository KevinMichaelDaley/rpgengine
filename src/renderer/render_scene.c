#include "ferrum/renderer/render_scene.h"

#include <stddef.h>

void render_scene_init(render_scene_t *scene, render_renderable_t *backing,
                       uint32_t capacity)
{
    if (scene == NULL) {
        return;
    }
    scene->items = backing;
    scene->count = 0u;
    scene->capacity = (backing != NULL) ? capacity : 0u;
    scene->dynamic_from = (backing != NULL) ? capacity : 0u; /* all static. */
    scene->lights = NULL;
}

void render_scene_mark_dynamic(render_scene_t *scene)
{
    if (scene != NULL) {
        scene->dynamic_from = scene->count;
    }
}

bool render_scene_add(render_scene_t *scene, const static_mesh_t *mesh,
                      const render_material_t *material, const float model[16])
{
    if (scene == NULL || scene->items == NULL || model == NULL) {
        return false;
    }
    if (scene->count >= scene->capacity) {
        return false;
    }
    render_renderable_t *r = &scene->items[scene->count++];
    r->mesh = mesh;
    r->material = material;
    for (int i = 0; i < 16; ++i) {
        r->model[i] = model[i];
    }
    r->sh_layer = 0; /* caller may override for per-chunk lightmap pages. */
    return true;
}

void render_scene_clear(render_scene_t *scene)
{
    if (scene != NULL) {
        scene->count = 0u;
        scene->dynamic_from = scene->capacity; /* all static again. */
    }
}

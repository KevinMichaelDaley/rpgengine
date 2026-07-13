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
    scene->lights = NULL;
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
    return true;
}

void render_scene_clear(render_scene_t *scene)
{
    if (scene != NULL) {
        scene->count = 0u;
    }
}

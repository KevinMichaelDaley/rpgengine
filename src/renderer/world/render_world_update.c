/**
 * @file render_world_update.c
 * @brief render_world per-frame update (rpg-i3wx): GI dispatch + forward render.
 */
#include "ferrum/renderer/render_world.h"

void render_world_update(render_world_t *rw, const gi_collider_t *boxes,
                         uint32_t n_boxes, int screen_w, int screen_h)
{
    if (rw == NULL || rw->scene == NULL) return;
    if (rw->gi_enabled)
        gi_runtime_frame(&rw->gi, rw->scene, rw->scene->camera.view,
                         rw->scene->camera.proj, boxes, n_boxes, screen_w, screen_h);
    render_forward_render(&rw->forward, rw->scene);
}

#include "ferrum/renderer/light_store.h"

#include <stddef.h>

void render_light_store_init(render_light_store_t *store,
                             render_light_t *backing, uint32_t capacity)
{
    if (store == NULL) {
        return;
    }
    store->lights = backing;
    store->count = 0u;
    store->capacity = (backing != NULL) ? capacity : 0u;
}

bool render_light_add(render_light_store_t *store, const render_light_t *light)
{
    if (store == NULL || light == NULL || store->lights == NULL) {
        return false;
    }
    if (store->count >= store->capacity) {
        return false;
    }
    store->lights[store->count++] = *light;
    return true;
}

void render_light_store_clear(render_light_store_t *store)
{
    if (store != NULL) {
        store->count = 0u;
    }
}

uint32_t render_light_store_pack(const render_light_store_t *store,
                                 int32_t *out_type, float *out_pos,
                                 float *out_dir, float *out_color,
                                 float *out_range, float *out_ci, float *out_co,
                                 uint32_t max)
{
    if (store == NULL || store->lights == NULL) {
        return 0u;
    }
    uint32_t n = 0u;
    for (uint32_t i = 0u; i < store->count && n < max; ++i) {
        const render_light_t *l = &store->lights[i];
        /* Only realtime punctual lights feed the forward+ shader; baked-only
         * and area lights are handled by the lightmap bake. */
        if ((l->flags & RENDER_LIGHT_FLAG_REALTIME) == 0u) {
            continue;
        }
        if (l->kind != RENDER_LIGHT_POINT && l->kind != RENDER_LIGHT_DIRECTIONAL &&
            l->kind != RENDER_LIGHT_SPOT) {
            continue;
        }
        out_type[n] = (int32_t)l->kind;
        for (int c = 0; c < 3; ++c) {
            out_pos[n * 3 + c] = l->position[c];
            out_dir[n * 3 + c] = l->direction[c];
            out_color[n * 3 + c] = l->color[c] * l->intensity;
        }
        out_range[n] = l->range;
        out_ci[n] = l->cos_inner;
        out_co[n] = l->cos_outer;
        ++n;
    }
    return n;
}

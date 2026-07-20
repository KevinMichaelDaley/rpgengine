/**
 * @file world_desc_query.c
 * @brief World-descriptor spatial + config queries (rpg-da8c / rpg-yrnu): which
 *        zone a point is in, and a zone's effective render-config path.
 */
#include "ferrum/scene/world_desc.h"

int world_desc_zone_at(const world_desc_t *w, const float p[3])
{
    if (w == NULL || p == NULL) return -1;
    for (uint32_t i = 0; i < w->zone_count; ++i) {
        const world_zone_t *z = &w->zones[i];
        if (p[0] >= z->box_min[0] && p[0] <= z->box_max[0] &&
            p[1] >= z->box_min[1] && p[1] <= z->box_max[1] &&
            p[2] >= z->box_min[2] && p[2] <= z->box_max[2])
            return (int)i;   /* first containing zone wins (zones may overlap). */
    }
    return -1;
}

const char *world_desc_zone_config(const world_desc_t *w, uint32_t i)
{
    if (w == NULL || i >= w->zone_count) return NULL;
    const char *own = w->zones[i].render_config;
    return (own[0] != '\0') ? own : w->default_render_config;
}

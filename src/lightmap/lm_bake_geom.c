/**
 * @file lm_bake_geom.c
 * @brief Bake geometry phase: combined luxels, atlas, SVO stamping (see header).
 */
#include "lm_bake_internal.h"

#include "ferrum/lightmap/lm_sh.h"
#include "ferrum/lightmap/lm_svo_material.h"
#include "ferrum/physics/mesh_collider.h"

/* Split a surface quad into two triangles (CCW about the surface normal). */
static void lm_bake_surface_tris(const lm_surface_t *s, phys_triangle_t tris[2])
{
    vec3_t c00 = s->origin;
    vec3_t c10 = vec3_add(s->origin, s->edge_u);
    vec3_t c01 = vec3_add(s->origin, s->edge_v);
    vec3_t c11 = vec3_add(c10, s->edge_v);
    tris[0].v[0] = c00; tris[0].v[1] = c10; tris[0].v[2] = c11;
    tris[1].v[0] = c00; tris[1].v[1] = c11; tris[1].v[2] = c01;
}

/* Fill one surface's luxels into the combined array at @p base, recording each
 * luxel's centre in @p positions and equal-split patch area in @p areas. */
static void lm_bake_fill_surface(const lm_surface_t *s, lm_luxel_t *luxels,
                                 vec3_t *positions, float *areas, uint32_t base)
{
    uint32_t count = s->res_u * s->res_v;
    float patch_area = (count > 0) ? lm_surface_area(s) / (float)count : 0.0f;
    for (uint32_t iv = 0; iv < s->res_v; ++iv) {
        for (uint32_t iu = 0; iu < s->res_u; ++iu) {
            uint32_t idx = base + iv * s->res_u + iu;
            lm_luxel_t *lx = &luxels[idx];
            lx->pos = lm_surface_point(s, iu, iv);
            lx->normal = s->normal;
            lx->albedo = s->albedo;
            lx->emissive = s->emissive;
            for (int c = 0; c < 3; ++c)
                lm_sh9_zero(&lx->sh[c]);
            positions[idx] = lx->pos;
            areas[idx] = patch_area;
        }
    }
}

bool lm_bake_build_geometry(const lm_scene_t *scene,
                            const lm_bake_config_t *cfg, lm_bake_result_t *res,
                            npc_svo_grid_t *svo, vec3_t **out_positions,
                            arena_t *arena)
{
    uint32_t n = scene->n_surfaces;

    /* Prefix-sum surface luxel counts -> total + per-surface offsets. */
    res->surface_offsets =
        arena_alloc(arena, _Alignof(uint32_t), (n + 1) * sizeof(uint32_t));
    res->rects = arena_alloc(arena, _Alignof(lm_atlas_rect_t),
                             (n ? n : 1) * sizeof(lm_atlas_rect_t));
    if (!res->surface_offsets || !res->rects)
        return false;
    uint32_t total = 0;
    for (uint32_t i = 0; i < n; ++i) {
        res->surface_offsets[i] = total;
        res->rects[i].w = scene->surfaces[i].res_u;
        res->rects[i].h = scene->surfaces[i].res_v;
        res->rects[i].x = res->rects[i].y = 0;
        total += scene->surfaces[i].res_u * scene->surfaces[i].res_v;
    }
    res->surface_offsets[n] = total;
    res->n_surfaces = n;
    res->n_luxels = total;

    res->combined.luxels =
        arena_alloc(arena, _Alignof(lm_luxel_t), (total ? total : 1) * sizeof(lm_luxel_t));
    res->luxel_areas =
        arena_alloc(arena, _Alignof(float), (total ? total : 1) * sizeof(float));
    vec3_t *positions =
        arena_alloc(arena, _Alignof(vec3_t), (total ? total : 1) * sizeof(vec3_t));
    if (!res->combined.luxels || !res->luxel_areas || !positions)
        return false;
    res->combined.res_u = total; /* linear: the whole array is one row */
    res->combined.res_v = 1;

    /* Fill luxels + stamp each surface into the SVO with its material id. */
    for (uint32_t i = 0; i < n; ++i) {
        lm_bake_fill_surface(&scene->surfaces[i], res->combined.luxels,
                             positions, res->luxel_areas,
                             res->surface_offsets[i]);
        phys_triangle_t tris[2];
        lm_bake_surface_tris(&scene->surfaces[i], tris);
        lm_svo_stamp_mesh(svo, tris, 2, scene->surface_materials[i]);
    }

    /* Pack the per-surface tiles into the output atlas. */
    if (!lm_atlas_pack(res->rects, n, cfg->atlas_width, cfg->atlas_padding,
                       &res->atlas))
        return false;

    *out_positions = positions;
    return true;
}

/**
 * @file client_scene_load.c
 * @brief Descriptor-driven client scene load + render + teardown (rpg-8302):
 *        the reusable counterpart of hall_lit_dynamic.c's inline assembly.
 */
#include <glad/glad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/renderer/client_scene.h"
#include "ferrum/scene/scene_desc.h"
#include "ferrum/probe/probe_place.h"
#include "ferrum/memory/arena.h"

/* ── Small helpers ─────────────────────────────────────────────── */

/* Column-major model matrix from translation/quaternion/scale. */
static void model_from_trs(const float p[3], const float q[4], const float s[3],
                           float m[16])
{
    float x = q[0], y = q[1], z = q[2], w = q[3];
    m[0]  = (1 - 2 * (y * y + z * z)) * s[0]; m[1] = (2 * (x * y + z * w)) * s[0];
    m[2]  = (2 * (x * z - y * w)) * s[0];     m[3] = 0;
    m[4]  = (2 * (x * y - z * w)) * s[1];     m[5] = (1 - 2 * (x * x + z * z)) * s[1];
    m[6]  = (2 * (y * z + x * w)) * s[1];     m[7] = 0;
    m[8]  = (2 * (x * z + y * w)) * s[2];     m[9] = (2 * (y * z - x * w)) * s[2];
    m[10] = (1 - 2 * (x * x + y * y)) * s[2]; m[11] = 0;
    m[12] = p[0]; m[13] = p[1]; m[14] = p[2]; m[15] = 1;
}

/* Read a whole file into a malloc'd buffer (caller frees). NULL on error. */
static uint8_t *read_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    uint8_t *buf = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long sz = ftell(f);
        if (sz > 0 && fseek(f, 0, SEEK_SET) == 0) {
            buf = malloc((size_t)sz);
            if (buf != NULL && fread(buf, 1, (size_t)sz, f) == (size_t)sz)
                *out_size = (size_t)sz;
            else { free(buf); buf = NULL; }
        }
    }
    fclose(f);
    return buf;
}

/* Load a mesh (.fvma) into a GPU static mesh. Returns 0 on success. */
static int load_mesh(const gl_loader_t *loader, const char *path, static_mesh_t *out)
{
    size_t sz = 0;
    uint8_t *bytes = read_file(path, &sz);
    if (bytes == NULL) return -1;
    int rc = static_mesh_create_from_fvma(loader, bytes, sz, out);
    free(bytes);
    return rc;
}

/* Decode + upload one material texture into cs->textures[slot_idx]; point the
 * material map at it. Missing files are skipped (map stays NULL). */
static void load_material_tex(client_scene_t *cs, render_material_t *mat,
                              int map_slot, const char *base_dir, const char *rel,
                              texture_format_t fmt, client_image_load_fn image_load)
{
    if (rel == NULL || rel[0] == '\0' || image_load == NULL) return;
    char path[512];
    snprintf(path, sizeof path, "%s/%s", base_dir, rel);
    int w = 0, h = 0; unsigned char *px = NULL;
    if (!image_load(path, &w, &h, &px) || px == NULL) return;
    texture_t *tex = &cs->textures[cs->texture_count];
    if (texture_create(tex, cs->loader) == TEXTURE_OK &&
        texture_upload_2d(tex, fmt, (uint32_t)w, (uint32_t)h, px, true) == TEXTURE_OK) {
        texture_set_sampler(tex, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_REPEAT, GL_REPEAT);
        mat->maps[map_slot] = tex;
        cs->texture_count++;
    }
    free(px);
}

/* Build a render material from a descriptor material definition. */
static void build_material(client_scene_t *cs, render_material_t *mat,
                           const scene_desc_material_t *md, const char *base_dir,
                           client_image_load_fn image_load)
{
    material_init(mat);
    for (int a = 0; a < 3; ++a) { mat->tint[a] = md->tint[a]; mat->emissive_color[a] = md->emissive_color[a]; }
    mat->metalness = md->metalness;
    mat->roughness_min = md->roughness_min;
    mat->roughness_max = md->roughness_max;
    mat->normal_scale = md->normal_scale;
    mat->uv_scale[0] = md->uv_scale[0]; mat->uv_scale[1] = md->uv_scale[1];
    mat->contrast = md->contrast;
    mat->ao_strength = md->ao_strength;
    mat->emissive_strength = md->emissive_strength;
    mat->orm_packed = md->orm_packed;
    static const struct { int map; int fmt_srgb; } slot[SCENE_DESC_MAT_TEX_COUNT] = {
        { MATERIAL_TEX_ALBEDO, 1 }, { MATERIAL_TEX_NORMAL, 0 },
        { MATERIAL_TEX_METALLIC, 0 }, { MATERIAL_TEX_ROUGHNESS, 0 },
        { MATERIAL_TEX_AO, 0 }, { MATERIAL_TEX_EMISSIVE, 1 },
    };
    for (int t = 0; t < SCENE_DESC_MAT_TEX_COUNT; ++t) {
        texture_format_t fmt = slot[t].fmt_srgb ? TEXTURE_FORMAT_SRGB8 : TEXTURE_FORMAT_RGB8;
        load_material_tex(cs, mat, slot[t].map, base_dir, md->tex[t], fmt, image_load);
    }
}

/* ── Public API ────────────────────────────────────────────────── */

bool client_scene_load(client_scene_t *cs, const gl_loader_t *loader,
                       const struct scene_desc *descp, const char *base_dir,
                       client_image_load_fn image_load, int screen_w, int screen_h)
{
    if (cs == NULL || loader == NULL || descp == NULL || base_dir == NULL) return false;
    const scene_desc_t *desc = descp;
    memset(cs, 0, sizeof *cs);
    cs->loader = loader;

    uint32_t nobj = desc->object_count, nmat = desc->material_count;
    cs->rb_cap = nobj + 64u;   /* slack for networked dynamic bodies. */
    cs->meshes = calloc(nobj ? nobj : 1, sizeof(static_mesh_t));
    cs->materials = calloc(nmat ? nmat : 1, sizeof(render_material_t));
    cs->textures = calloc((nmat ? nmat : 1) * SCENE_DESC_MAT_TEX_COUNT, sizeof(texture_t));
    cs->rb = calloc(cs->rb_cap, sizeof(render_renderable_t));
    cs->light_buf = calloc(64, sizeof(render_light_t));
    if (!cs->meshes || !cs->materials || !cs->textures || !cs->rb || !cs->light_buf) {
        client_scene_destroy(cs); return false;
    }

    for (uint32_t m = 0; m < nmat; ++m)
        build_material(cs, &cs->materials[m], &desc->materials[m], base_dir, image_load);

    /* Meshes + scene AABB (transformed mesh bounds). */
    float amin[3] = { 1e30f, 1e30f, 1e30f }, amax[3] = { -1e30f, -1e30f, -1e30f };
    render_scene_init(&cs->scene, cs->rb, cs->rb_cap);
    int *sh_layer = calloc(nobj ? nobj : 1, sizeof(int));
    for (uint32_t i = 0; i < nobj; ++i) {
        const scene_desc_object_t *o = &desc->objects[i];
        char path[512]; snprintf(path, sizeof path, "%s/%s", base_dir, o->mesh);
        if (load_mesh(loader, path, &cs->meshes[cs->mesh_count]) != 0) continue;
        static_mesh_t *sm = &cs->meshes[cs->mesh_count];
        float model[16]; model_from_trs(o->position, o->rotation, o->scale, model);
        int mi = (o->material_count > 0) ? o->material_idx[0] : -1;
        const render_material_t *mat = (mi >= 0 && (uint32_t)mi < nmat) ? &cs->materials[mi] : NULL;
        if (render_scene_add(&cs->scene, sm, mat, model)) {
            cs->scene.items[cs->scene.count - 1].sh_layer =
                (sh_layer && i < nobj) ? sh_layer[i] : 0;
        }
        for (int cx = 0; cx < 8; ++cx) {
            float lp[3] = { (cx & 1) ? sm->aabb_max[0] : sm->aabb_min[0],
                            (cx & 2) ? sm->aabb_max[1] : sm->aabb_min[1],
                            (cx & 4) ? sm->aabb_max[2] : sm->aabb_min[2] };
            float wp[3] = {
                model[0]*lp[0]+model[4]*lp[1]+model[8]*lp[2]+model[12],
                model[1]*lp[0]+model[5]*lp[1]+model[9]*lp[2]+model[13],
                model[2]*lp[0]+model[6]*lp[1]+model[10]*lp[2]+model[14] };
            for (int a = 0; a < 3; ++a) { if (wp[a] < amin[a]) amin[a] = wp[a]; if (wp[a] > amax[a]) amax[a] = wp[a]; }
        }
        cs->mesh_count++;
    }
    cs->static_count = (int)cs->scene.count;
    if (amin[0] > amax[0]) { for (int a = 0; a < 3; ++a) { amin[a] = -10; amax[a] = 10; } }

    /* Baked lightmap SH arrays. */
    if (desc->lightdata.lightmap_prefix[0]) {
        char lm[512]; snprintf(lm, sizeof lm, "%s/%s", base_dir, desc->lightdata.lightmap_prefix);
        int *sl = calloc(nobj ? nobj : 1, sizeof(int));
        client_scene_load_lightmap(loader, lm, nobj, cs->sh_tex, sl);
        for (uint32_t i = 0; i < nobj && i < cs->scene.count; ++i)
            cs->scene.items[i].sh_layer = sl ? sl[i] : 0;
        free(sl);
    }
    free(sh_layer);

    render_light_store_init(&cs->lights, cs->light_buf, 64);
    cs->scene.lights = &cs->lights;

    /* Probe grid from the descriptor spec. */
    static uint8_t probe_arena_buf[4 * 1024 * 1024];
    arena_t pa; arena_init(&pa, probe_arena_buf, sizeof probe_arena_buf);
    probe_set_t pset; memset(&pset, 0, sizeof pset);
    probe_place_grid(&desc->probes, amin, amax, &pa, &pset);

    /* render_world config: forward + GI, from the descriptor. */
    render_world_config_t cfg; memset(&cfg, 0, sizeof cfg);
    cfg.forward.loader = loader;
    cfg.forward.cluster = (cluster_config_t){ 16, 16, 24, 0.2f, 60.0f };
    cfg.forward.max_lights = 512;
    cfg.forward.index_capacity = 16u * 16u * 24u * 16u;
    cfg.forward.screen_w = (float)screen_w; cfg.forward.screen_h = (float)screen_h;
    cfg.forward.ambient[0] = cfg.forward.ambient[1] = cfg.forward.ambient[2] = 0.05f;
    cfg.forward.sh_enabled = cs->sh_tex[0] ? 1 : 0;
    cfg.forward.sh_scale = 0.7f; cfg.forward.sh_normal_bias = 1.0f;
    for (int c = 0; c < 9; ++c) cfg.forward.sh_tex[c] = cs->sh_tex[c];
    cfg.scene = &cs->scene;

    cfg.gi_enabled = 1;
    cfg.gi_sdf_prefix = NULL;   /* set below to a persistent string. */
    static char sdf_path[512];
    if (desc->lightdata.sdf_prefix[0]) {
        snprintf(sdf_path, sizeof sdf_path, "%s/%s", base_dir, desc->lightdata.sdf_prefix);
        cfg.gi_sdf_prefix = sdf_path;
    }
    for (int a = 0; a < 3; ++a) { cfg.gi_aabb_min[a] = amin[a]; cfg.gi_aabb_max[a] = amax[a]; }
    cfg.gi_aabb_min[1] += 0.3f; cfg.gi_aabb_max[1] -= 0.2f;
    cfg.gi_probe_pos = pset.positions; cfg.gi_probe_count = pset.count;
    cfg.gi_grid_cell = 4.0f;
    cfg.gi_prepass_w = (screen_w / 8 > 0) ? screen_w / 8 : 1;
    cfg.gi_prepass_h = (screen_h / 8 > 0) ? screen_h / 8 : 1;
    cfg.gi_max_lights = 512; cfg.gi_max_boxes = 64; cfg.gi_soft_k = 8.0f;
    cfg.gi_probe_min = 4; cfg.gi_probe_sphere_margin = 1.2f; cfg.gi_bin_interval = 1;
    cfg.gi_update_interval = 8; cfg.gi_n_probe_groups = 2;
    if (pset.grid_dim[0] > 0) {
        for (int a = 0; a < 3; ++a) {
            cfg.gi_grid_origin[a] = pset.grid_origin[a];
            cfg.gi_grid_cell3[a] = pset.grid_cell[a];
            cfg.gi_grid_dim[a] = pset.grid_dim[a];
        }
    }
    cfg.has_static_weights = 1; cfg.static_baked_w = 0.35f; cfg.static_dyn_w = 3.0f;
    cfg.has_spec_gain = 1; cfg.spec_gain = 1.0f;

    if (!render_world_init(&cs->world, &cfg)) { client_scene_destroy(cs); return false; }
    return true;
}

void client_scene_render(client_scene_t *cs, const render_camera_t *cam,
                         const gi_collider_t *boxes, uint32_t n_boxes,
                         int screen_w, int screen_h)
{
    if (cs == NULL || cam == NULL) return;
    cs->scene.camera = *cam;
    render_world_update(&cs->world, boxes, n_boxes, screen_w, screen_h);
}

void client_scene_destroy(client_scene_t *cs)
{
    if (cs == NULL) return;
    render_world_destroy(&cs->world);
    for (uint32_t i = 0; i < cs->mesh_count; ++i) static_mesh_destroy(&cs->meshes[i]);
    for (uint32_t i = 0; i < cs->texture_count; ++i) texture_destroy(&cs->textures[i]);
    for (int c = 0; c < 9; ++c) if (cs->sh_tex[c]) glDeleteTextures(1, &cs->sh_tex[c]);
    free(cs->meshes); free(cs->materials); free(cs->textures);
    free(cs->rb); free(cs->light_buf);
    memset(cs, 0, sizeof *cs);
}

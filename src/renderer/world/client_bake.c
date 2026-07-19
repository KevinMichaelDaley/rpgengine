/**
 * @file client_bake.c
 * @brief In-client lightmap bake (rpg-jro2): build an lm_mesh_scene from a loaded
 *        level descriptor + its fvma meshes (world-space, uv1) and run the GPU
 *        lightmap gather. The bake counterpart of scene_bake.c, but driven by the
 *        client scene loader instead of an offline Blender codegen -- so the
 *        geometry can be streamed to a baking client over the network.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/renderer/client_bake.h"
#include "ferrum/scene/scene_desc.h"
#include "ferrum/lightmap/lm_bake_driver.h"
#include "ferrum/lightmap/lm_mesh.h"
#include "ferrum/lightmap/lm_light.h"
#include "ferrum/lightmap/lm_image.h"
#include "ferrum/editor/mesh/mesh_vao_format.h"
#include "ferrum/editor/mesh/mesh_slot.h"
#include "ferrum/memory/arena.h"

/* Bake job context handed to the lm_bake_driver setup callback. All backing that
 * must outlive the bake lives here (slots) or in the driver arena (world verts). */
typedef struct client_bake_ctx {
    const scene_desc_t     *desc;
    const char             *base_dir;
    client_image_load_fn    img_load;
    mesh_slot_t            *slots;    /**< [object_count] CPU meshes (kept alive). */
    lm_image_t             *images;   /**< [material_count] albedo textures. */
    lm_mesh_t              *meshes;   /**< [object_count] arena-built. */
    lm_light_t              sun;
    uint32_t                have_sun;
} client_bake_ctx_t;

static float env_f(const char *k, float d) { const char *v = getenv(k); return v ? (float)atof(v) : d; }
static uint32_t env_u(const char *k, uint32_t d) { const char *v = getenv(k); return v ? (uint32_t)strtoul(v, NULL, 10) : d; }

/* Column-major model matrix from translation/quaternion(xyzw)/scale. */
static void model_from_trs(const float p[3], const float q[4], const float s[3], float m[16])
{
    float x = q[0], y = q[1], z = q[2], w = q[3];
    m[0] = (1 - 2 * (y * y + z * z)) * s[0]; m[1] = (2 * (x * y + z * w)) * s[0];
    m[2] = (2 * (x * z - y * w)) * s[0];     m[3] = 0;
    m[4] = (2 * (x * y - z * w)) * s[1];     m[5] = (1 - 2 * (x * x + z * z)) * s[1];
    m[6] = (2 * (y * z + x * w)) * s[1];     m[7] = 0;
    m[8] = (2 * (x * z + y * w)) * s[2];     m[9] = (2 * (y * z - x * w)) * s[2];
    m[10] = (1 - 2 * (x * x + y * y)) * s[2]; m[11] = 0;
    m[12] = p[0]; m[13] = p[1]; m[14] = p[2]; m[15] = 1;
}

/* Load a material's albedo PNG into an lm_image (once); NULL image on failure. */
static void load_albedo(client_bake_ctx_t *c, uint32_t mat, lm_image_t *out)
{
    memset(out, 0, sizeof *out);
    if (mat >= c->desc->material_count || c->img_load == NULL) return;
    const char *rel = c->desc->materials[mat].tex[SCENE_DESC_MAT_TEX_ALBEDO];
    if (rel[0] == '\0') return;
    char path[512]; snprintf(path, sizeof path, "%s/%s", c->base_dir, rel);
    int w = 0, h = 0; unsigned char *px = NULL;
    if (c->img_load(path, &w, &h, &px) && px != NULL) {
        out->pixels = px; out->width = (uint32_t)w; out->height = (uint32_t)h;
        out->channels = 3; out->srgb = true;
    }
}

/* lm_bake_driver setup: build the world-space mesh scene + bake config. */
static bool client_bake_setup(lm_mesh_scene_t *scene, lm_bake_config_t *cfg,
                              arena_t *arena, void *user)
{
    client_bake_ctx_t *c = user;
    const scene_desc_t *d = c->desc;
    const float lmscale = env_f("CLIENT_BAKE_LMSCALE", 0.5f);   /* <1 shrinks the atlas (VRAM). */

    for (uint32_t m = 0; m < d->material_count; ++m) load_albedo(c, m, &c->images[m]);

    float bmin[3] = { 1e30f, 1e30f, 1e30f }, bmax[3] = { -1e30f, -1e30f, -1e30f };
    uint32_t nm = 0;
    for (uint32_t i = 0; i < d->object_count; ++i) {
        const scene_desc_object_t *o = &d->objects[i];
        char path[512]; snprintf(path, sizeof path, "%s/%s", c->base_dir, o->mesh);
        size_t sz = 0; FILE *f = fopen(path, "rb");
        if (f == NULL) continue;
        fseek(f, 0, SEEK_END); long fl = ftell(f); fseek(f, 0, SEEK_SET);
        uint8_t *bytes = (fl > 0) ? malloc((size_t)fl) : NULL;
        if (bytes == NULL || fread(bytes, 1, (size_t)fl, f) != (size_t)fl) { free(bytes); fclose(f); continue; }
        fclose(f); sz = (size_t)fl;
        mesh_slot_t *slot = &c->slots[nm];
        memset(slot, 0, sizeof *slot);
        bool ok = mesh_vao_deserialize(bytes, sz, slot);
        free(bytes);
        if (!ok || slot->uvs[1] == NULL) { if (ok) mesh_slot_destroy(slot); continue; }

        /* World-space positions + normals (backing in the driver arena). */
        uint32_t vc = slot->vertex_count;
        float *wp = arena_alloc(arena, 16u, (size_t)vc * 3 * sizeof(float));
        float *wn = arena_alloc(arena, 16u, (size_t)vc * 3 * sizeof(float));
        if (wp == NULL || wn == NULL) { mesh_slot_destroy(slot); return false; }
        float mm[16]; model_from_trs(o->position, o->rotation, o->scale, mm);
        for (uint32_t v = 0; v < vc; ++v) {
            const float *lp = &slot->positions[v * 3];
            wp[v * 3 + 0] = mm[0]*lp[0] + mm[4]*lp[1] + mm[8]*lp[2] + mm[12];
            wp[v * 3 + 1] = mm[1]*lp[0] + mm[5]*lp[1] + mm[9]*lp[2] + mm[13];
            wp[v * 3 + 2] = mm[2]*lp[0] + mm[6]*lp[1] + mm[10]*lp[2] + mm[14];
            float nx = 0, ny = 1, nz = 0;
            if (slot->normals != NULL) { nx = slot->normals[v*3]; ny = slot->normals[v*3+1]; nz = slot->normals[v*3+2]; }
            float rx = mm[0]*nx + mm[4]*ny + mm[8]*nz;
            float ry = mm[1]*nx + mm[5]*ny + mm[9]*nz;
            float rz = mm[2]*nx + mm[6]*ny + mm[10]*nz;
            float ln = sqrtf(rx*rx + ry*ry + rz*rz); if (ln > 1e-8f) { rx/=ln; ry/=ln; rz/=ln; }
            wn[v*3] = rx; wn[v*3+1] = ry; wn[v*3+2] = rz;
            for (int a = 0; a < 3; ++a) { if (wp[v*3+a] < bmin[a]) bmin[a] = wp[v*3+a]; if (wp[v*3+a] > bmax[a]) bmax[a] = wp[v*3+a]; }
        }
        int32_t mi = (o->material_count > 0) ? o->material_idx[0] : -1;
        lm_mesh_t *lm = &c->meshes[nm];
        memset(lm, 0, sizeof *lm);
        lm->positions = wp; lm->normals = wn;
        lm->uv0 = slot->uvs[0]; lm->uv1 = slot->uvs[1];
        lm->indices = slot->indices; lm->vert_count = vc; lm->index_count = slot->index_count;
        lm->albedo_image = (mi >= 0 && c->images[mi].pixels) ? &c->images[mi] : NULL;
        lm->albedo = (vec3_t){ 1.0f, 1.0f, 1.0f };
        lm->emissive = (vec3_t){ 0.0f, 0.0f, 0.0f };
        lm->material = 0;
        int lr = (o->lightmap_res > 0) ? o->lightmap_res : 64;
        lm->lightmap_resolution = (uint32_t)(lmscale * (float)lr + 0.5f);
        if (lm->lightmap_resolution < 4u) lm->lightmap_resolution = 4u;
        ++nm;
    }
    if (nm == 0) return false;
    float diag = sqrtf((bmax[0]-bmin[0])*(bmax[0]-bmin[0]) + (bmax[1]-bmin[1])*(bmax[1]-bmin[1]) +
                       (bmax[2]-bmin[2])*(bmax[2]-bmin[2]));

    lm_material_t fb = { { 0, 0, 0 }, { 0, 0, 0 } };
    scene->meshes = c->meshes; scene->n_meshes = nm;
    scene->lights = c->have_sun ? &c->sun : NULL; scene->n_lights = c->have_sun ? 1u : 0u;
    scene->materials = (lm_material_table_t){ NULL, 0, fb };

    memset(cfg, 0, sizeof *cfg);
    float pad = 1.0f;
    cfg->svo_bounds = (phys_aabb_t){ { bmin[0]-pad, bmin[1]-pad, bmin[2]-pad },
                                     { bmax[0]+pad, bmax[1]+pad, bmax[2]+pad } };
    cfg->voxel_size = env_f("CLIENT_BAKE_VOXEL", 0.12f);       /* coarse -> smaller SDF/SVO */
    cfg->atlas_width = env_u("CLIENT_BAKE_ATLAS", 4096u);
    cfg->atlas_padding = 2u;
    cfg->direct_samples = 0u;
    cfg->farfield_samples = env_u("CLIENT_BAKE_SAMPLES", 512u);
    cfg->gi_bounces = env_u("CLIENT_BAKE_BOUNCES", 3u);
    cfg->gi_threads = 0u;
    cfg->gi_batch = env_u("CLIENT_BAKE_BATCH", 16u);           /* small batches -> less VRAM */
    cfg->farfield_near = 0.5f * diag;
    cfg->farfield_maxdist = 1e9f;
    cfg->seed = 11u;
    cfg->sky.kind = LM_SKY_CONSTANT;
    /* TODO(descriptor): emit sky radiance in the level file. Hall sky for now. */
    cfg->sky.color = (vec3_t){ 0.30780f, 0.37700f, 0.51760f };
    cfg->chunk_size = 0.0f;   /* keep existing _cNNN.sdf; bake the .flm only. */
    cfg->sdf_out_prefix = NULL;
    printf("[client_bake] %u meshes, atlas=%u lmscale=%.2f voxel=%.3f samples=%u bounces=%u diag=%.1f\n",
           nm, cfg->atlas_width, (double)lmscale, (double)cfg->voxel_size,
           cfg->farfield_samples, cfg->gi_bounces, (double)diag);
    fflush(stdout);
    return true;
}

bool client_bake_run(const gl_loader_t *loader, const struct scene_desc *descp,
                     const char *base_dir, client_image_load_fn image_load,
                     const char *out_flm)
{
    if (loader == NULL || descp == NULL || base_dir == NULL || out_flm == NULL) return false;
    const scene_desc_t *desc = descp;

    client_bake_ctx_t ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.desc = desc; ctx.base_dir = base_dir; ctx.img_load = image_load;
    ctx.slots = calloc(desc->object_count ? desc->object_count : 1, sizeof *ctx.slots);
    ctx.images = calloc(desc->material_count ? desc->material_count : 1, sizeof *ctx.images);
    ctx.meshes = calloc(desc->object_count ? desc->object_count : 1, sizeof *ctx.meshes);
    if (!ctx.slots || !ctx.images || !ctx.meshes) { free(ctx.slots); free(ctx.images); free(ctx.meshes); return false; }

    /* Sun: first descriptor directional light -> bake radiance = colour*intensity. */
    for (uint32_t i = 0; i < desc->light_count; ++i) {
        const scene_desc_light_t *l = &desc->lights[i];
        if (l->kind != SCENE_DESC_LIGHT_DIRECTIONAL) continue;
        memset(&ctx.sun, 0, sizeof ctx.sun);
        ctx.sun.kind = LM_LIGHT_DIRECTIONAL;
        ctx.sun.direction = (vec3_t){ l->direction[0], l->direction[1], l->direction[2] };
        ctx.sun.color = (vec3_t){ l->color[0]*l->intensity, l->color[1]*l->intensity, l->color[2]*l->intensity };
        ctx.have_sun = 1u; break;
    }

    size_t arena_mb = env_u("CLIENT_BAKE_ARENA_MB", 4096u);
    size_t abytes = arena_mb * 1024u * 1024u;
    void *abuf = malloc(abytes);
    if (abuf == NULL) { fprintf(stderr, "[client_bake] arena malloc(%zu MB) failed\n", arena_mb); }
    bool ok = false;
    if (abuf != NULL) {
        arena_t arena; arena_init(&arena, abuf, abytes);
        ok = lm_bake_driver_run(loader, client_bake_setup, &ctx, out_flm, &arena);
        free(abuf);
    }

    for (uint32_t i = 0; i < desc->object_count; ++i)
        if (ctx.slots[i].positions != NULL || ctx.slots[i].indices != NULL) mesh_slot_destroy(&ctx.slots[i]);
    for (uint32_t m = 0; m < desc->material_count; ++m)
        if (ctx.images[m].pixels != NULL) free((void *)ctx.images[m].pixels);
    free(ctx.slots); free(ctx.images); free(ctx.meshes);
    return ok;
}

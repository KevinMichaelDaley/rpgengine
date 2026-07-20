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
#include "ferrum/lightmap/lm_lightmap_file.h"
#include "ferrum/lightmap/gpu/lm_gpu_gather.h"
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

static uint32_t client_bake_meshes(client_bake_ctx_t *c, arena_t *arena,
                                   float bmin[3], float bmax[3]);
static void client_bake_config(lm_bake_config_t *cfg,
                               const float bmin[3], const float bmax[3]);

/* lm_bake_driver setup: build the world-space mesh scene + bake config. */
static bool client_bake_setup(lm_mesh_scene_t *scene, lm_bake_config_t *cfg,
                              arena_t *arena, void *user)
{
    client_bake_ctx_t *c = user;
    for (uint32_t m = 0; m < c->desc->material_count; ++m) load_albedo(c, m, &c->images[m]);
    float bmin[3], bmax[3];
    uint32_t nm = client_bake_meshes(c, arena, bmin, bmax);
    if (nm == 0) return false;
    lm_material_t fb = { { 0, 0, 0 }, { 0, 0, 0 } };
    scene->meshes = c->meshes; scene->n_meshes = nm;
    scene->lights = c->have_sun ? &c->sun : NULL; scene->n_lights = c->have_sun ? 1u : 0u;
    scene->materials = (lm_material_table_t){ NULL, 0, fb };
    client_bake_config(cfg, bmin, bmax);
    printf("[client_bake] %u meshes, atlas=%u voxel=%.3f samples=%u bounces=%u\n",
           nm, cfg->atlas_width, (double)cfg->voxel_size, cfg->farfield_samples, cfg->gi_bounces);
    fflush(stdout);
    return true;
}

/* Build a world-space lm_mesh_t for every loadable descriptor object into
 * c->meshes (vertex backing in @p arena) + keep the CPU slots alive in c->slots.
 * Returns the built mesh count and fills the world-space scene AABB. 0 => none.
 * Shared by the single-atlas and per-chunk bake paths (one source of truth). */
static uint32_t client_bake_meshes(client_bake_ctx_t *c, arena_t *arena,
                                   float bmin[3], float bmax[3])
{
    const scene_desc_t *d = c->desc;
    const float lmscale = env_f("CLIENT_BAKE_LMSCALE", 0.5f);   /* <1 shrinks the atlas (VRAM). */
    bmin[0] = bmin[1] = bmin[2] = 1e30f;
    bmax[0] = bmax[1] = bmax[2] = -1e30f;
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
    return nm;
}

/* Fill the bake config (atlas/samples/sky/SVO bounds) from the world-space scene
 * AABB. Shared by the single-atlas and per-chunk paths so every chunk bakes with
 * the same far-field, voxel, and sky settings. */
static void client_bake_config(lm_bake_config_t *cfg,
                               const float bmin[3], const float bmax[3])
{
    float diag = sqrtf((bmax[0]-bmin[0])*(bmax[0]-bmin[0]) + (bmax[1]-bmin[1])*(bmax[1]-bmin[1]) +
                       (bmax[2]-bmin[2])*(bmax[2]-bmin[2]));
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
}

/* --------------------------------------------------------------------------
 * Chunked lightmap bake (rpg-yfa4): partition the scene into a uniform chunk grid
 * and bake each chunk's meshes into their OWN atlas -- but always gather + occlude
 * against the FULL scene (config.geo_scene) so cross-chunk bounce + shadow stay
 * correct. Emits <prefix>_c%03u.flm per non-empty chunk + a ZLM1 <prefix>_manifest.bin
 * (per-mesh chunk id + atlas rect), the streamer's multi-chunk input. A level too
 * small for more than one cell is just the 1-chunk special case. Selected by the
 * CLIENT_BAKE_CHUNK env (chunk edge in metres) in client_bake_run.
 * -------------------------------------------------------------------------- */
static bool client_bake_chunked(const gl_loader_t *loader, client_bake_ctx_t *c,
                                const char *out_prefix, float chunk_size, arena_t *arena)
{
    for (uint32_t m = 0; m < c->desc->material_count; ++m) load_albedo(c, m, &c->images[m]);

    float bmin[3], bmax[3];
    uint32_t nm = client_bake_meshes(c, arena, bmin, bmax);
    if (nm == 0) { fprintf(stderr, "[client_bake] chunked: no bakeable meshes\n"); return false; }

    /* The full scene = the gather/occlusion geometry every chunk shares. */
    lm_material_t fb = { { 0, 0, 0 }, { 0, 0, 0 } };
    lm_mesh_scene_t full;
    memset(&full, 0, sizeof full);
    full.meshes = c->meshes; full.n_meshes = nm;
    full.lights = c->have_sun ? &c->sun : NULL; full.n_lights = c->have_sun ? 1u : 0u;
    full.materials = (lm_material_table_t){ NULL, 0, fb };

    /* Uniform chunk grid over the padded scene AABB. */
    const float pad = 1.0f;
    float gmin[3]; int gd[3];
    for (int a = 0; a < 3; ++a) {
        gmin[a] = bmin[a] - pad;
        float ext = (bmax[a] + pad) - gmin[a];
        gd[a] = (int)(ext / chunk_size) + 1;
        if (gd[a] < 1) gd[a] = 1;
    }
    int ncells = gd[0] * gd[1] * gd[2];

    /* Per-mesh working arrays (bake runs once -- malloc is fine, not per-frame). */
    int *cell2chunk = malloc((size_t)ncells * sizeof(int));
    int *mesh_chunk = malloc((size_t)nm * sizeof(int));
    int *out_chunk  = malloc((size_t)nm * sizeof(int));
    lm_atlas_rect_t *out_rect = malloc((size_t)nm * sizeof(lm_atlas_rect_t));
    lm_mesh_t *cmesh = malloc((size_t)nm * sizeof(lm_mesh_t));
    uint32_t  *gidx  = malloc((size_t)nm * sizeof(uint32_t));
    if (!cell2chunk || !mesh_chunk || !out_chunk || !out_rect || !cmesh || !gidx) {
        free(cell2chunk); free(mesh_chunk); free(out_chunk); free(out_rect); free(cmesh); free(gidx);
        return false;
    }

    /* Bucket each mesh into a cell by its vertex centroid; remap non-empty cells
     * to DENSE chunk ids (the streamer expects _c000.._cNNN with no gaps). */
    for (int i = 0; i < ncells; ++i) cell2chunk[i] = -1;
    int n_chunks = 0;
    for (uint32_t i = 0; i < nm; ++i) {
        const lm_mesh_t *lm = &c->meshes[i];
        double cx = 0, cy = 0, cz = 0;
        for (uint32_t v = 0; v < lm->vert_count; ++v) {
            cx += lm->positions[v*3]; cy += lm->positions[v*3+1]; cz += lm->positions[v*3+2];
        }
        double inv = lm->vert_count ? 1.0 / (double)lm->vert_count : 0.0;
        float ctr[3] = { (float)(cx*inv), (float)(cy*inv), (float)(cz*inv) };
        int idx[3];
        for (int a = 0; a < 3; ++a) {
            idx[a] = (int)((ctr[a] - gmin[a]) / chunk_size);
            if (idx[a] < 0) idx[a] = 0;
            if (idx[a] >= gd[a]) idx[a] = gd[a] - 1;
        }
        int cell = (idx[2] * gd[1] + idx[1]) * gd[0] + idx[0];
        if (cell2chunk[cell] < 0) cell2chunk[cell] = n_chunks++;
        mesh_chunk[i] = cell2chunk[cell];
    }

    /* Stand up the GPU gather once; every chunk bakes on the same context. */
    if (loader != NULL && !lm_gpu_gather_init(loader)) {
        free(cell2chunk); free(mesh_chunk); free(out_chunk); free(out_rect); free(cmesh); free(gidx);
        return false;
    }

    /* Per-chunk world AABB (min[3],max[3]) -> appended to the manifest so the
     * streamer can drive proximity residency for open worlds. */
    float *cbox = malloc((size_t)n_chunks * 6u * sizeof(float));
    if (cbox == NULL) {
        if (loader != NULL) lm_gpu_gather_shutdown();
        free(cell2chunk); free(mesh_chunk); free(out_chunk); free(out_rect); free(cmesh); free(gidx);
        return false;
    }
    for (int cc = 0; cc < n_chunks; ++cc) {
        cbox[cc*6+0] = cbox[cc*6+1] = cbox[cc*6+2] = 1e30f;
        cbox[cc*6+3] = cbox[cc*6+4] = cbox[cc*6+5] = -1e30f;
    }

    size_t mk = arena_mark(arena);      /* full-scene verts live below this mark. */
    uint32_t max_aw = 0, max_ah = 0;
    bool ok = true;
    for (int cc = 0; cc < n_chunks && ok; ++cc) {
        /* Gather this chunk's meshes (subset that is luxelized/atlased) + grow
         * the chunk's world box over their vertices. */
        uint32_t k = 0;
        for (uint32_t i = 0; i < nm; ++i)
            if (mesh_chunk[i] == cc) {
                cmesh[k] = c->meshes[i]; gidx[k] = i;
                const lm_mesh_t *lm = &c->meshes[i];
                for (uint32_t v = 0; v < lm->vert_count; ++v)
                    for (int a = 0; a < 3; ++a) {
                        float p = lm->positions[v*3+a];
                        if (p < cbox[cc*6+a])   cbox[cc*6+a]   = p;
                        if (p > cbox[cc*6+3+a]) cbox[cc*6+3+a] = p;
                    }
                ++k;
            }

        lm_mesh_scene_t cs;
        memset(&cs, 0, sizeof cs);
        cs.meshes = cmesh; cs.n_meshes = k;
        cs.lights = full.lights; cs.n_lights = full.n_lights;
        cs.materials = full.materials;

        lm_bake_config_t cfg;
        client_bake_config(&cfg, bmin, bmax);   /* shared far-field for every chunk. */
        cfg.geo_scene = &full;      /* gather + occlude against the whole level. */
        if (loader != NULL) cfg.gpu_gather = 1;

        arena_pop_to_mark(arena, mk);            /* reset the per-chunk bake scratch. */
        lm_mesh_bake_result_t result;
        if (!lm_mesh_bake(&cs, &cfg, &result, arena)) { ok = false; break; }

        char path[600];
        snprintf(path, sizeof path, "%s_c%03u.flm", out_prefix, (unsigned)cc);
        if (!lm_lightmap_save(&result, path)) { ok = false; break; }

        for (uint32_t j = 0; j < k; ++j) { out_chunk[gidx[j]] = cc; out_rect[gidx[j]] = result.rects[j]; }
        if (result.atlas.width  > max_aw) max_aw = result.atlas.width;
        if (result.atlas.height > max_ah) max_ah = result.atlas.height;
        printf("[client_bake] chunk %d/%d: %u meshes -> %s (%ux%u)\n",
               cc, n_chunks, k, path, result.atlas.width, result.atlas.height);
        fflush(stdout);
    }

    /* ZLM1 manifest: "ZLM1" + {n_meshes, n_chunks, max_aw, max_ah} + per-mesh
     * {int32 chunk, uint32 r[4]={x,y,w,h}} (the reader repacks into {w,h,x,y}). */
    if (ok) {
        char mp[600];
        snprintf(mp, sizeof mp, "%s_manifest.bin", out_prefix);
        FILE *mf = fopen(mp, "wb");
        if (mf == NULL) ok = false;
        else {
            uint32_t hdr[4] = { nm, (uint32_t)n_chunks, max_aw, max_ah };
            ok = (fwrite("ZLM1", 1, 4, mf) == 4) && (fwrite(hdr, sizeof hdr, 1, mf) == 1);
            for (uint32_t i = 0; ok && i < nm; ++i) {
                int32_t L = out_chunk[i];
                uint32_t r[4] = { out_rect[i].x, out_rect[i].y, out_rect[i].w, out_rect[i].h };
                ok = (fwrite(&L, 4, 1, mf) == 1) && (fwrite(r, 4, 4, mf) == 4);
            }
            /* Optional trailer: per-chunk world AABB (6 floats each). Older readers
             * stop after the per-mesh records; the client reads these for proximity. */
            if (ok) ok = fwrite(cbox, sizeof(float), (size_t)n_chunks * 6u, mf) == (size_t)n_chunks * 6u;
            fclose(mf);
            printf("[client_bake] wrote %s (%u meshes, %d chunks, atlas %ux%u)\n",
                   mp, nm, n_chunks, max_aw, max_ah);
            fflush(stdout);
        }
    }

    if (loader != NULL) lm_gpu_gather_shutdown();
    free(cbox);
    free(cell2chunk); free(mesh_chunk); free(out_chunk); free(out_rect); free(cmesh); free(gidx);
    return ok;
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
        /* CLIENT_BAKE_CHUNK > 0 (chunk edge in metres) -> per-chunk atlas + ZLM1
         * manifest for the streamer; unset/0 keeps the single global atlas. */
        float chunk_edge = env_f("CLIENT_BAKE_CHUNK", 0.0f);
        if (chunk_edge > 0.0f) {
            /* Chunk prefix = out_flm without a trailing ".flm" (baker appends _cNNN). */
            char prefix[512];
            snprintf(prefix, sizeof prefix, "%s", out_flm);
            size_t pl = strlen(prefix);
            if (pl > 4 && strcmp(prefix + (pl - 4), ".flm") == 0) prefix[pl - 4] = '\0';
            ok = client_bake_chunked(loader, &ctx, prefix, chunk_edge, &arena);
        } else {
            ok = lm_bake_driver_run(loader, client_bake_setup, &ctx, out_flm, &arena);
        }
        free(abuf);
    }

    for (uint32_t i = 0; i < desc->object_count; ++i)
        if (ctx.slots[i].positions != NULL || ctx.slots[i].indices != NULL) mesh_slot_destroy(&ctx.slots[i]);
    for (uint32_t m = 0; m < desc->material_count; ++m)
        if (ctx.images[m].pixels != NULL) free((void *)ctx.images[m].pixels);
    free(ctx.slots); free(ctx.images); free(ctx.meshes);
    return ok;
}

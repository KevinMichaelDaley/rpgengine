/**
 * @file hall_bake.c
 * @brief Headless (no-GL) bake of the romanesque hall's directional sun into an
 *        SH lightmap, serialized to a .flm. Meant to run on a many-core box; the
 *        GL render (hall_bake_render) then loads the .flm with SKIP_BAKE.
 *
 * Usage: hall_bake <dmesh_dir> <bake_tex_dir> <out.flm>
 * Env: HALL_VOXEL (m, default 0.02), HALL_SAMPLES (default 256),
 *      HALL_BOUNCES (default 2), HALL_THREADS (0 = all cores).
 */
#include <dirent.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glad/glad.h>
#ifdef HALL_EGL
#include "ferrum/renderer/egl_headless.h"
#else
#include <SDL2/SDL.h>
#endif
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "ferrum/lightmap/gpu/lm_gpu_gather.h"
#include "ferrum/lightmap/lm_image.h"
#include "ferrum/lightmap/lm_lightmap_file.h"
#include "ferrum/lightmap/lm_mesh_bake.h"
#include "ferrum/renderer/gl_loader.h"

#ifndef HALL_EGL
static void *hb_getproc(const char *n, void *u) { (void)u; return SDL_GL_GetProcAddress(n); }
#endif
#include "ferrum/math/vec3.h"
#include "ferrum/memory/arena.h"
#include "ferrum/mesh/dmesh_loader.h"

#define MAXM 32

static vec3_t v3(float x, float y, float z) { return (vec3_t){ x, y, z }; }

static int group_of(const char *n)
{
    if (strstr(n, "win") || strstr(n, "door")) return 0;
    if (strstr(n, "vault")) return 2;
    return 1;
}

static void load_cpu(const char *path, lm_image_t *img, int srgb)
{
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 3);
    if (!px) { img->pixels = NULL; return; }
    img->pixels = px; img->width = (uint32_t)w; img->height = (uint32_t)h;
    img->channels = 3; img->srgb = srgb != 0;
}

/* Progressive-batch hook: re-serialize the running lightmap after every gather
 * batch so an external viewer can render the refining result mid-bake. */
struct hall_batch_ctx { lm_mesh_bake_result_t *res; const char *out; };
static void hall_on_batch(void *ud, uint32_t done, uint32_t total)
{
    struct hall_batch_ctx *b = (struct hall_batch_ctx *)ud;
    lm_lightmap_save(b->res, b->out);
    fprintf(stderr, "batch %u/%u -> %s\n", done, total, b->out);
    fflush(stderr);
}

int main(int argc, char **argv)
{
    const char *dir  = argc > 1 ? argv[1] : "datasets/hall_lm";
    const char *bake = argc > 2 ? argv[2] : "assets/arch/proc/prefabs/bake";
    const char *out  = argc > 3 ? argv[3] : "/tmp/hall.flm";

    obj_mesh_t dm[MAXM]; int grp[MAXM]; int nm = 0; char names[MAXM][128];
    DIR *d = opendir(dir); struct dirent *e;
    while (d && (e = readdir(d)) && nm < MAXM) {
        if (!strstr(e->d_name, ".dmesh")) continue;
        char p[512]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (dmesh_load(p, &dm[nm]) == 0) {
            grp[nm] = group_of(e->d_name);
            snprintf(names[nm], sizeof names[nm], "%s", e->d_name);
            ++nm;
        }
    }
    if (d) closedir(d);
    printf("loaded %d dual-uv meshes\n", nm);

    char pab[512], pas[512], pav[512];
    snprintf(pab, sizeof pab, "%s/albedo.png", bake);
    snprintf(pas, sizeof pas, "%s/ashlar_albedo.png", bake);
    snprintf(pav, sizeof pav, "%s/vault_albedo.png", bake);
    lm_image_t im_b, im_s, im_v;
    load_cpu(pab, &im_b, 1); load_cpu(pas, &im_s, 1); load_cpu(pav, &im_v, 1);
    const lm_image_t *grp_img[3] = { &im_b, &im_s, &im_v };

    static char abuf[900 * 1024 * 1024]; arena_t arena;
    arena_init(&arena, abuf, sizeof abuf);
    lm_mesh_t lms[MAXM];
    float bmin[3] = { 1e30f, 1e30f, 1e30f }, bmax[3] = { -1e30f, -1e30f, -1e30f };
    for (int i = 0; i < nm; ++i) {
        memset(&lms[i], 0, sizeof(lm_mesh_t));
        lms[i].positions = dm[i].positions; lms[i].normals = dm[i].normals;
        lms[i].uv0 = dm[i].uvs; lms[i].uv1 = dm[i].uvs1;
        lms[i].indices = dm[i].indices; lms[i].vert_count = dm[i].vert_count;
        lms[i].index_count = dm[i].index_count;
        lms[i].albedo_image = grp_img[grp[i]]; lms[i].emissive_image = NULL;
        lms[i].albedo = v3(1, 1, 1); lms[i].emissive = v3(0, 0, 0);
        lms[i].material = 0;
        lms[i].lightmap_resolution =
            strstr(names[i], "col")   ? 224u :
            strstr(names[i], "resp")  ? 128u :
            strstr(names[i], "floor") ? 160u :
            (grp[i] == 0 ? 80u : 72u);
        for (uint32_t v = 0; v < dm[i].vert_count; ++v)
            for (int c = 0; c < 3; ++c) {
                float p = dm[i].positions[v*3+c];
                if (p < bmin[c]) bmin[c] = p;
                if (p > bmax[c]) bmax[c] = p;
            }
    }
    float diag = sqrtf((bmax[0]-bmin[0])*(bmax[0]-bmin[0]) +
                       (bmax[1]-bmin[1])*(bmax[1]-bmin[1]) +
                       (bmax[2]-bmin[2])*(bmax[2]-bmin[2]));
    float half_diag = 0.5f * diag;

    lm_light_t sun; memset(&sun, 0, sizeof sun); sun.kind = LM_LIGHT_DIRECTIONAL;
    sun.direction = v3(0.15f, -0.42f, 0.90f); sun.color = v3(3.6f, 3.4f, 3.0f);
    lm_material_t fb = { { 0, 0, 0 }, { 0, 0, 0 } };
    lm_mesh_scene_t scene = { lms, (uint32_t)nm, &sun, 1, { NULL, 0, fb } };

    lm_bake_config_t cfg = { 0 };
    cfg.svo_bounds = (phys_aabb_t){ { -1.5f, -0.5f, -7.5f }, { 10.5f, 5.5f, 1.5f } };
    cfg.voxel_size = getenv("HALL_VOXEL") ? (float)atof(getenv("HALL_VOXEL")) : 0.02f;
    cfg.atlas_width = 4096; cfg.atlas_padding = 2; cfg.direct_samples = 0;
    cfg.farfield_samples = getenv("HALL_SAMPLES") ? (uint32_t)atoi(getenv("HALL_SAMPLES")) : 256u;
    cfg.gi_bounces = getenv("HALL_BOUNCES") ? (uint32_t)atoi(getenv("HALL_BOUNCES")) : 2u;
    cfg.gi_threads = getenv("HALL_THREADS") ? (uint32_t)atoi(getenv("HALL_THREADS")) : 0u;
    cfg.farfield_near = half_diag; cfg.farfield_maxdist = 1e9f; cfg.seed = 11u;
    cfg.sky.kind = LM_SKY_CONSTANT; cfg.sky.color = v3(0.55f, 0.68f, 0.95f);

    printf("baking hall: voxel=%.3fm samples=%u bounces=%u diag=%.2f\n",
           cfg.voxel_size, cfg.farfield_samples, cfg.gi_bounces, diag);
    fflush(stdout);
    cfg.gi_batch = getenv("HALL_BATCH") ? (uint32_t)atoi(getenv("HALL_BATCH")) : 64u;

    /* Optional GPU gather (rpg-k4lk): stand up a GL 4.3+ compute context. With
     * HALL_EGL the context is surfaceless (headless GPU box, no X/SDL); otherwise
     * a hidden SDL window is used. */
    if (getenv("HALL_GPU")) {
#ifdef HALL_EGL
        if (!egl_headless_init(4, 3)) { fprintf(stderr, "egl init failed\n"); return 1; }
        if (!gladLoadGLLoader((GLADloadproc)egl_headless_getproc_glad)) { fprintf(stderr, "glad failed\n"); return 1; }
        gl_loader_t gl_loader = { egl_headless_getproc, NULL };
#else
        if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL init failed\n"); return 1; }
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_Window *gpuwin = SDL_CreateWindow("hallbake", 0, 0, 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        SDL_GLContext gpuctx = SDL_GL_CreateContext(gpuwin); SDL_GL_MakeCurrent(gpuwin, gpuctx);
        (void)gpuctx;
        if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) { fprintf(stderr, "glad failed\n"); return 1; }
        gl_loader_t gl_loader = { hb_getproc, NULL };
#endif
        if (!lm_gpu_gather_init(&gl_loader)) { fprintf(stderr, "gpu gather init failed\n"); return 1; }
        cfg.gpu_gather = 1;
        printf("GPU gather ENABLED: %s\n", (const char *)glGetString(GL_RENDERER));
        fflush(stdout);
    }

    lm_mesh_bake_result_t res;
    struct hall_batch_ctx bctx = { &res, out };
    cfg.on_batch = hall_on_batch;
    cfg.on_batch_ud = &bctx;
    if (!lm_mesh_bake(&scene, &cfg, &res, &arena)) {
        fprintf(stderr, "bake failed\n");
        return 1;
    }
    printf("baked %u luxels into %ux%u atlas\n", res.n_luxels,
           res.atlas.width, res.atlas.height);
    if (!lm_lightmap_save(&res, out)) {
        fprintf(stderr, "save failed\n");
        return 1;
    }
    printf("wrote %s\n", out);
    return 0;
}

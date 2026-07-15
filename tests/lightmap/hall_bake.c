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
#include "ferrum/lightmap/lm_bake_driver.h"
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
static int hb_cmpstr(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

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

/* --- Hall scene, loaded once into file-scope storage and assembled by the
 * hall_setup callback that the generic bake driver (lm_bake_driver) invokes.
 * The dmeshes/textures/meshes/sun must outlive the bake, hence file scope. --- */
static obj_mesh_t g_dm[MAXM];
static lm_mesh_t  g_lms[MAXM];
static lm_image_t g_im_b, g_im_s, g_im_v;
static lm_light_t g_sun;
struct hall_input { const char *dir; const char *bake; };

/* Build the romanesque-hall scene + bake config (rpg-fzht: one caller of the
 * generic driver). @p user is a struct hall_input with the dmesh dir + texture
 * dir; env vars tune voxel/samples/bounces/lmres. */
static bool hall_setup(lm_mesh_scene_t *scene, lm_bake_config_t *cfg,
                       arena_t *arena, void *user)
{
    (void)arena;
    struct hall_input *in = (struct hall_input *)user;
    const char *dir = in->dir, *bake = in->bake;

    /* Collect + SORT the .dmesh filenames. readdir() order is filesystem/machine
     * dependent, so baking on one box and rendering on another would otherwise
     * assign each mesh a different atlas index -> every surface samples the wrong
     * atlas region (splotches). A deterministic sort makes the mesh order (and
     * thus the baked rects) identical on every machine. The renderer sorts too. */
    int grp[MAXM]; char names[MAXM][128]; int nm = 0;
    char fnames[MAXM][128]; int nf = 0;
    DIR *d = opendir(dir); struct dirent *e;
    while (d && (e = readdir(d)) && nf < MAXM) {
        if (!strstr(e->d_name, ".dmesh")) continue;
        snprintf(fnames[nf], sizeof fnames[nf], "%s", e->d_name); ++nf;
    }
    if (d) closedir(d);
    qsort(fnames, (size_t)nf, sizeof fnames[0], hb_cmpstr);
    for (int fi = 0; fi < nf; ++fi) {
        char p[512]; snprintf(p, sizeof p, "%s/%s", dir, fnames[fi]);
        if (dmesh_load(p, &g_dm[nm]) == 0) {
            grp[nm] = group_of(fnames[fi]);
            snprintf(names[nm], sizeof names[nm], "%s", fnames[fi]);
            ++nm;
        }
    }
    printf("loaded %d dual-uv meshes\n", nm);
    if (nm == 0) return false;

    char pab[512], pas[512], pav[512];
    snprintf(pab, sizeof pab, "%s/albedo.png", bake);
    snprintf(pas, sizeof pas, "%s/ashlar_albedo.png", bake);
    snprintf(pav, sizeof pav, "%s/vault_albedo.png", bake);
    load_cpu(pab, &g_im_b, 1); load_cpu(pas, &g_im_s, 1); load_cpu(pav, &g_im_v, 1);
    const lm_image_t *grp_img[3] = { &g_im_b, &g_im_s, &g_im_v };

    /* Per-mesh lightmap texel resolution, scaled by HALL_LMRES (default 1). Crank
     * it for a high-res production bake (more, sharper luxels). */
    float lmres = getenv("HALL_LMRES") ? (float)atof(getenv("HALL_LMRES")) : 1.0f;
    float bmin[3] = { 1e30f, 1e30f, 1e30f }, bmax[3] = { -1e30f, -1e30f, -1e30f };
    for (int i = 0; i < nm; ++i) {
        memset(&g_lms[i], 0, sizeof(lm_mesh_t));
        g_lms[i].positions = g_dm[i].positions; g_lms[i].normals = g_dm[i].normals;
        g_lms[i].uv0 = g_dm[i].uvs; g_lms[i].uv1 = g_dm[i].uvs1;
        g_lms[i].indices = g_dm[i].indices; g_lms[i].vert_count = g_dm[i].vert_count;
        g_lms[i].index_count = g_dm[i].index_count;
        g_lms[i].albedo_image = grp_img[grp[i]]; g_lms[i].emissive_image = NULL;
        g_lms[i].albedo = v3(1, 1, 1); g_lms[i].emissive = v3(0, 0, 0);
        g_lms[i].material = 0;
        uint32_t base_lmres =
            strstr(names[i], "col")   ? 224u :
            strstr(names[i], "resp")  ? 128u :
            strstr(names[i], "floor") ? 160u :
            (grp[i] == 0 ? 80u : 72u);
        g_lms[i].lightmap_resolution = (uint32_t)(lmres * (float)base_lmres + 0.5f);
        for (uint32_t v = 0; v < g_dm[i].vert_count; ++v)
            for (int c = 0; c < 3; ++c) {
                float p = g_dm[i].positions[v*3+c];
                if (p < bmin[c]) bmin[c] = p;
                if (p > bmax[c]) bmax[c] = p;
            }
    }
    float diag = sqrtf((bmax[0]-bmin[0])*(bmax[0]-bmin[0]) +
                       (bmax[1]-bmin[1])*(bmax[1]-bmin[1]) +
                       (bmax[2]-bmin[2])*(bmax[2]-bmin[2]));

    memset(&g_sun, 0, sizeof g_sun); g_sun.kind = LM_LIGHT_DIRECTIONAL;
    g_sun.direction = v3(0.15f, -0.42f, 0.90f); g_sun.color = v3(3.6f, 3.4f, 3.0f);
    lm_material_t fb = { { 0, 0, 0 }, { 0, 0, 0 } };
    *scene = (lm_mesh_scene_t){ g_lms, (uint32_t)nm, &g_sun, 1, { NULL, 0, fb } };

    memset(cfg, 0, sizeof *cfg);
    cfg->svo_bounds = (phys_aabb_t){ { -1.5f, -0.5f, -7.5f }, { 10.5f, 5.5f, 1.5f } };
    cfg->voxel_size = getenv("HALL_VOXEL") ? (float)atof(getenv("HALL_VOXEL")) : 0.02f;
    cfg->atlas_width = 4096; cfg->atlas_padding = 2; cfg->direct_samples = 0;
    cfg->farfield_samples = getenv("HALL_SAMPLES") ? (uint32_t)atoi(getenv("HALL_SAMPLES")) : 256u;
    cfg->gi_bounces = getenv("HALL_BOUNCES") ? (uint32_t)atoi(getenv("HALL_BOUNCES")) : 2u;
    cfg->gi_threads = getenv("HALL_THREADS") ? (uint32_t)atoi(getenv("HALL_THREADS")) : 0u;
    cfg->farfield_near = 0.5f * diag; cfg->farfield_maxdist = 1e9f; cfg->seed = 11u;
    cfg->sky.kind = LM_SKY_CONSTANT; cfg->sky.color = v3(0.55f, 0.68f, 0.95f);
    cfg->gi_batch = getenv("HALL_BATCH") ? (uint32_t)atoi(getenv("HALL_BATCH")) : 64u;

    printf("baking hall: voxel=%.3fm samples=%u bounces=%u diag=%.2f\n",
           cfg->voxel_size, cfg->farfield_samples, cfg->gi_bounces, diag);
    fflush(stdout);
    return true;
}

int main(int argc, char **argv)
{
    struct hall_input in = {
        argc > 1 ? argv[1] : "datasets/hall_lm",
        argc > 2 ? argv[2] : "assets/arch/proc/prefabs/bake",
    };
    const char *out = argc > 3 ? argv[3] : "/tmp/hall.flm";

    static char abuf[900 * 1024 * 1024]; arena_t arena;
    arena_init(&arena, abuf, sizeof abuf);

    /* Optional GPU compute context. With HALL_EGL it is surfaceless (headless GPU
     * box, no X/SDL); otherwise a hidden SDL window is used. The driver runs the
     * gather on it; with no HALL_GPU the driver bakes on the CPU. */
    gl_loader_t gl_loader; const gl_loader_t *gl = NULL;
    if (getenv("HALL_GPU")) {
#ifdef HALL_EGL
        if (!egl_headless_init(4, 3)) { fprintf(stderr, "egl init failed\n"); return 1; }
        if (!gladLoadGLLoader((GLADloadproc)egl_headless_getproc_glad)) { fprintf(stderr, "glad failed\n"); return 1; }
        gl_loader = (gl_loader_t){ egl_headless_getproc, NULL };
#else
        if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL init failed\n"); return 1; }
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_Window *gpuwin = SDL_CreateWindow("hallbake", 0, 0, 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        SDL_GLContext gpuctx = SDL_GL_CreateContext(gpuwin); SDL_GL_MakeCurrent(gpuwin, gpuctx);
        (void)gpuctx;
        if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) { fprintf(stderr, "glad failed\n"); return 1; }
        gl_loader = (gl_loader_t){ hb_getproc, NULL };
#endif
        gl = &gl_loader;
        printf("GPU gather ENABLED: %s\n", (const char *)glGetString(GL_RENDERER));
        fflush(stdout);
    }

    if (!lm_bake_driver_run(gl, hall_setup, &in, out, &arena)) {
        fprintf(stderr, "bake failed\n");
        return 1;
    }
    printf("wrote %s\n", out);
    return 0;
}

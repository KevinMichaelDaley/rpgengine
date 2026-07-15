/**
 * @file lm_bake_driver_tests.c
 * @brief Unit tests for the generic bake driver (lm_bake_driver): a scene-setup
 *        callback drives an (optionally GPU) bake and serializes the result.
 *        CPU path only here so the test stays headless (no GL context).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/lightmap/lm_bake_driver.h"
#include "ferrum/lightmap/lm_lightmap_file.h"
#include "ferrum/memory/arena.h"

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); ++g_fail; } \
                              else printf("ok: %s\n", msg); } while (0)

/* A minimal single-quad emissive scene. Backing arrays live in the arena. */
static bool tiny_setup(lm_mesh_scene_t *scene, lm_bake_config_t *cfg,
                       arena_t *arena, void *user)
{
    int *called = (int *)user;
    if (called) *called += 1;

    float *pos = arena_alloc(arena, 16u, 4 * 3 * sizeof(float));
    float *nrm = arena_alloc(arena, 16u, 4 * 3 * sizeof(float));
    float *uv  = arena_alloc(arena, 16u, 4 * 2 * sizeof(float));
    uint32_t *idx = arena_alloc(arena, 16u, 6 * sizeof(uint32_t));
    lm_mesh_t *m  = arena_alloc(arena, 16u, sizeof(lm_mesh_t));
    if (!pos || !nrm || !uv || !idx || !m) return false;

    const float P[12] = { 0,0,0,  1,0,0,  1,0,1,  0,0,1 };      /* floor quad, +Y up */
    const float N[12] = { 0,1,0,  0,1,0,  0,1,0,  0,1,0 };
    const float U[8]  = { 0,0,  1,0,  1,1,  0,1 };
    const uint32_t I[6] = { 0,1,2, 0,2,3 };
    memcpy(pos, P, sizeof P); memcpy(nrm, N, sizeof N);
    memcpy(uv, U, sizeof U);  memcpy(idx, I, sizeof I);

    memset(m, 0, sizeof *m);
    m->positions = pos; m->normals = nrm; m->uv0 = uv; m->uv1 = uv; m->indices = idx;
    m->vert_count = 4; m->index_count = 6;
    m->albedo = (vec3_t){ 1, 1, 1 };
    m->emissive = (vec3_t){ 2, 2, 2 };     /* emits so the bake has signal */
    m->lightmap_resolution = 16u;

    memset(scene, 0, sizeof *scene);
    scene->meshes = m; scene->n_meshes = 1;

    memset(cfg, 0, sizeof *cfg);
    cfg->svo_bounds = (phys_aabb_t){ { -0.5f, -0.5f, -0.5f }, { 1.5f, 1.5f, 1.5f } };
    cfg->voxel_size = 0.1f;
    cfg->atlas_width = 128; cfg->atlas_padding = 2;
    cfg->farfield_samples = 16u; cfg->gi_bounces = 1u; cfg->gi_batch = 16u;
    cfg->seed = 1u;
    cfg->sky.kind = LM_SKY_CONSTANT; cfg->sky.color = (vec3_t){ 0.2f, 0.2f, 0.2f };
    return true;
}

static bool fail_setup(lm_mesh_scene_t *scene, lm_bake_config_t *cfg,
                       arena_t *arena, void *user)
{
    (void)scene; (void)cfg; (void)arena; (void)user;
    return false; /* abort */
}

int main(void)
{
    static char buf[64 * 1024 * 1024];
    arena_t arena; arena_init(&arena, buf, sizeof buf);
    const char *out = "/tmp/lm_bake_driver_test.flm";
    remove(out);

    /* Happy path: CPU bake (gl=NULL) writes a loadable flm. */
    int called = 0;
    arena_reset(&arena);
    bool ok = lm_bake_driver_run(NULL, tiny_setup, &called, out, &arena);
    CHECK(ok, "driver run (CPU) succeeds");
    CHECK(called == 1, "setup callback invoked exactly once");

    lm_lightmap_data_t lm; memset(&lm, 0, sizeof lm);
    bool loaded = lm_lightmap_load(out, &lm);
    CHECK(loaded, "written flm loads back");
    CHECK(loaded && lm.n_meshes == 1, "flm records the one mesh");
    CHECK(loaded && lm.atlas_w > 0 && lm.atlas_h > 0, "flm has a non-empty atlas");
    if (loaded) lm_lightmap_data_free(&lm);

    /* Failure modes. */
    arena_reset(&arena);
    CHECK(!lm_bake_driver_run(NULL, NULL, NULL, out, &arena), "NULL setup -> false");
    CHECK(!lm_bake_driver_run(NULL, tiny_setup, &called, NULL, &arena), "NULL out_path -> false");
    arena_reset(&arena);
    CHECK(!lm_bake_driver_run(NULL, fail_setup, NULL, out, &arena), "setup returning false -> false");

    printf(g_fail ? "\n%d FAILURES\n" : "\nALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}

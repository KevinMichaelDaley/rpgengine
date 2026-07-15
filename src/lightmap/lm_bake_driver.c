/**
 * @file lm_bake_driver.c
 * @brief Generic lightmap bake driver (see lm_bake_driver.h).
 */
#include "ferrum/lightmap/lm_bake_driver.h"

#include <stdio.h>
#include <string.h>

#include "ferrum/lightmap/gpu/lm_gpu_gather.h"
#include "ferrum/lightmap/lm_lightmap_file.h"

/* Progressive-save hook: re-serialize the running lightmap after each gather
 * batch so an external viewer can watch the result refine mid-bake. */
struct lm_bake_batch_ctx {
    lm_mesh_bake_result_t *result;
    const char            *out_path;
};

static void lm_bake_on_batch(void *ud, uint32_t done, uint32_t total)
{
    struct lm_bake_batch_ctx *b = (struct lm_bake_batch_ctx *)ud;
    lm_lightmap_save(b->result, b->out_path);
    fprintf(stderr, "[bake] batch %u/%u -> %s\n", done, total, b->out_path);
    fflush(stderr);
}

bool lm_bake_driver_run(const gl_loader_t *gl, lm_bake_setup_fn setup, void *user,
                        const char *out_path, arena_t *arena)
{
    if (setup == NULL || out_path == NULL || arena == NULL)
        return false;

    lm_mesh_scene_t scene;
    lm_bake_config_t config;
    memset(&scene, 0, sizeof scene);
    memset(&config, 0, sizeof config);
    if (!setup(&scene, &config, arena, user))
        return false;

    /* GPU path: stand up the compute gather on the caller's context. */
    if (gl != NULL) {
        if (!lm_gpu_gather_init(gl))
            return false;
        config.gpu_gather = 1;
    }

    lm_mesh_bake_result_t result;
    struct lm_bake_batch_ctx bctx = { &result, out_path };
    config.on_batch = lm_bake_on_batch;
    config.on_batch_ud = &bctx;

    if (!lm_mesh_bake(&scene, &config, &result, arena))
        return false;
    if (!lm_lightmap_save(&result, out_path))
        return false;
    return true;
}

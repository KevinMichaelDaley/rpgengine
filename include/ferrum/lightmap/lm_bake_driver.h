/**
 * @file lm_bake_driver.h
 * @brief Generic lightmap bake driver: a scene-setup callback populates the
 *        scene + config, the driver runs the (optionally GPU) gather and writes
 *        the result. Decouples the bake orchestration from any one scene, so the
 *        hall bake, tests, and future region/chunk bakers are all just callers.
 *
 * Ownership: the callback allocates its scene backing arrays from the caller's
 * arena; they must outlive the call. Nullability: all pointer args are required
 * except @p user. Error semantics: returns false on a NULL arg, a callback that
 * returns false, GPU init failure, a bake failure, or an IO error. Side effects:
 * writes @p out_path (re-serialized after every gather batch for live preview);
 * if @p gl is non-NULL, initialises the file-static GPU gather state.
 */
#ifndef FERRUM_LIGHTMAP_LM_BAKE_DRIVER_H
#define FERRUM_LIGHTMAP_LM_BAKE_DRIVER_H

#include <stdbool.h>

#include "ferrum/lightmap/lm_mesh_bake.h"
#include "ferrum/memory/arena.h"
#include "ferrum/renderer/gl_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Populate @p scene and @p config for one bake. Allocate any backing
 *        arrays from @p arena (they must outlive the bake). @p user is the opaque
 *        pointer given to @ref lm_bake_driver_run. Return false to abort.
 */
typedef bool (*lm_bake_setup_fn)(lm_mesh_scene_t *scene, lm_bake_config_t *config,
                                 arena_t *arena, void *user);

/**
 * @brief Run one bake. If @p gl is non-NULL the GPU gather is stood up on that
 *        context and @c config->gpu_gather is forced on; otherwise the CPU gather
 *        is used. @p setup builds the scene/config, then the baked result is
 *        written to @p out_path. Returns false on setup/GPU-init/bake/IO failure.
 */
bool lm_bake_driver_run(const gl_loader_t *gl, lm_bake_setup_fn setup, void *user,
                        const char *out_path, arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_BAKE_DRIVER_H */

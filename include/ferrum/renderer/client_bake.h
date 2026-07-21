/**
 * @file client_bake.h
 * @brief In-client lightmap bake: bake the SH lightmap for a loaded level scene
 *        using the client's own compute-ready GL context (rpg-jro2).
 *
 * The bake is a CLIENT MODE, not a separate offline tool: it loads the same
 * static objects the renderer loads (descriptor + fvma meshes, which carry the
 * lightmap uv1), builds an lm_mesh_scene from them + the descriptor's sun, and
 * runs the GPU lightmap gather (lm_bake_driver) to write <out>.flm. Because it
 * reuses the streamed scene loader, the geometry can be delivered over the
 * network (stream the static assets to a baking client), not just from disk.
 *
 * VRAM: the gather + atlas live on the GPU; the config defaults are conservative
 * (2048 atlas, coarse voxels, small batches) and env-overridable so a modest GPU
 * does not OOM. See CLIENT_BAKE_* env vars in client_bake.c.
 *
 * Ownership: allocates a large transient arena internally (freed on return);
 * borrows @p loader / @p desc. Requires a current GL 4.3+ (compute) context.
 */
#ifndef FERRUM_RENDERER_CLIENT_BAKE_H
#define FERRUM_RENDERER_CLIENT_BAKE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/client_scene.h" /* client_image_load_fn */

struct scene_desc;

/**
 * @brief Bake the SH lightmap for @p desc's static meshes to @p out_flm using the
 *        current GL context's compute gather.
 * @param loader     GL entry points (compute-ready 4.3+ context must be current).
 * @param desc       parsed level descriptor (objects + sun light).
 * @param base_dir   directory the descriptor's relative asset paths resolve under.
 * @param image_load caller image decoder for material albedo textures (nullable).
 * @param out_flm    output .flm path (per-mesh atlas rects trail the SH images).
 * @return true on success; false on load/bake/IO error. Never crashes.
 */
bool client_bake_run(const gl_loader_t *loader, const struct scene_desc *desc,
                     const char *base_dir, client_image_load_fn image_load,
                     const char *out_flm);

/**
 * @brief Compose + write the GLOBAL low-res ZONE SDF ("<prefix>_zone.sdf") from
 *        the on-disk fine chunks ("<prefix>_cNNN.sdf"): the runtime's page-fault
 *        fallback (min-downsampled -- conservative, thin walls survive).
 * @param sdf_prefix chunk path prefix (same value as CLIENT_BAKE_SDF).
 * @param max_dim    coarse cells along the longest zone extent (e.g. 64).
 * @return true if the zone file was written. Headless except file IO; no GL.
 */
bool client_bake_zone_sdf(const char *sdf_prefix, int max_dim);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_CLIENT_BAKE_H */

/**
 * @file gi_vis_prepass.h
 * @brief Low-res visibility prepass that finds the on-screen chunk set so
 *        per-chunk resources page into bounded GPU residency (rpg-ojuq/rpg-fo9r).
 *
 * Renders the scene into a low-res R32UI target, each fragment writing a chunk id
 * (+1); a readback yields the visible chunk set. TWO modes:
 *
 *   - MESH mode (@ref gi_vis_prepass_run_mesh): the chunk id is a per-mesh
 *     uniform (the SH lightmap: each mesh belongs to one atlas chunk).
 *   - WORLD mode (@ref gi_vis_prepass_run_world): the chunk id is computed
 *     PER FRAGMENT from its world position against the SDF chunk boxes -- so a
 *     mesh spanning a chunk boundary correctly marks every SDF chunk its surface
 *     actually covers (a per-mesh centroid mapping would miss boundary chunks).
 *
 * Both fill @c visible[]. GL-only (glad). Owns its FBO/target/programs/readback.
 */
#ifndef FERRUM_RENDERER_GI_GI_VIS_PREPASS_H
#define FERRUM_RENDERER_GI_GI_VIS_PREPASS_H

#include <stdint.h>

#include "ferrum/renderer/render_scene.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Max SDF chunk boxes the world-mode fragment shader tests per fragment. */
/* Max SDF chunk boxes the visibility prepass classifies. Bounded by fragment
 * uniform space: 128 vec3 pairs = 768 components, within the GL 3.3 minimum of
 * 1024. Adaptive bakes produce ~100+ chunks, so 64 was too low. */
#define GI_VIS_MAX_BOXES 128

/** The chunk-id prepass + its visible-chunk result. */
typedef struct gi_vis_prepass {
    unsigned int prog_mesh, prog_world;  /**< the two chunk-id programs. */
    unsigned int prog_dual;              /**< dual chunk-id program (0 until enabled). */
    unsigned int fbo, col, depth;        /**< R32UI colour + depth. */
    int          w, h;
    uint32_t    *read;                   /**< w*h readback buffer. */
    uint8_t     *visible;                /**< [n_chunks] SDF/world visible flags (1 frame late). */
    int          n_chunks;               /**< sizing of visible[] (>= both modes). */
    uint8_t     *visible_lm;             /**< [n_lm_chunks] lightmap-chunk visible flags (dual). */
    int          n_lm_chunks;            /**< sizing of visible_lm[] (0 if dual disabled). */
    unsigned int pbo[2];                 /**< ping-pong pack buffers for async readback. */
    int          cur;                    /**< which pbo this frame writes. */
    int          primed;                 /**< frames until the async pipeline is warm. */
} gi_vis_prepass_t;

/**
 * @brief Create the prepass at @p w x @p h; @p n_chunks sizes @c visible[] (pass
 *        the max of the SH-chunk and SDF-chunk counts). Returns 0, or -1.
 */
int gi_vis_prepass_init(gi_vis_prepass_t *pp, int w, int h, int n_chunks);

/**
 * @brief MESH mode: draw @p scene with a per-mesh chunk id (@p mchunk[i], -1 =
 *        skip) and fill @c visible[]. @p view/@p proj column-major.
 */
void gi_vis_prepass_run_mesh(gi_vis_prepass_t *pp, const render_scene_t *scene,
                             const float view[16], const float proj[16],
                             const int *mchunk, int nm, int main_w, int main_h);

/**
 * @brief WORLD mode: draw @p scene; each fragment's chunk id is the first SDF
 *        chunk box (@p box_min/@p box_max, 3 floats each, @p n_boxes entries,
 *        <= GI_VIS_MAX_BOXES) that contains its world position. Fills
 *        @c visible[]. Assumes identity model transforms (world == vertex pos).
 */
void gi_vis_prepass_run_world(gi_vis_prepass_t *pp, const render_scene_t *scene,
                              const float view[16], const float proj[16],
                              const float *box_min, const float *box_max,
                              int n_boxes, int main_w, int main_h);

/**
 * @brief Enable the DUAL prepass: allocate the lightmap-chunk visible set +
 *        compile the dual program. Call once after init. @p n_lm_chunks sizes
 *        @c visible_lm[]. Returns 0, or -1 on OOM/compile failure.
 */
int gi_vis_prepass_enable_dual(gi_vis_prepass_t *pp, int n_lm_chunks);

/**
 * @brief DUAL mode: ONE geometry pass writes BOTH chunk index sets to different
 *        bit-fields of the R32UI target -- the SDF chunk id (per fragment, first
 *        containing box in @p box_min/@p box_max, @p n_boxes) in the low 16 bits,
 *        and the lightmap chunk id (per mesh, @p mchunk[i], -1 = none) in the high
 *        16 bits. Fills @c visible (SDF) AND @c visible_lm (lightmap). Because SDF
 *        voxel chunks and lightmap atlas chunks have independent boundaries, both
 *        index sets are gathered together here. Requires gi_vis_prepass_enable_dual.
 */
void gi_vis_prepass_run_dual(gi_vis_prepass_t *pp, const render_scene_t *scene,
                             const float view[16], const float proj[16],
                             const float *box_min, const float *box_max, int n_boxes,
                             const int *mchunk, int nm, int main_w, int main_h);

/** @brief Free GL + host resources. NULL-safe. */
void gi_vis_prepass_destroy(gi_vis_prepass_t *pp);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_GI_GI_VIS_PREPASS_H */

/**
 * @file refl_bake_lm.h
 * @brief Bake-mode cube renderer over the lightmap mesh set (rpg-wlh9):
 *        --bake owns the lm_mesh_t array (world-space, indexed, per-mesh
 *        albedo/emissive tints), so reflection cubes rasterize those
 *        directly with the minimal lit shader -- fully self-contained GL,
 *        none of the live pipeline's target/viewport/clear state hazards.
 *
 * Ownership: upload creates VBO/IBO/VAO GL objects until destroy; the
 * mesh array is borrowed and must outlive the set. Bake-time only.
 */
#ifndef FERRUM_RENDERER_GI_REFL_BAKE_LM_H
#define FERRUM_RENDERER_GI_REFL_BAKE_LM_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/lightmap/lm_mesh.h"
#include "ferrum/renderer/gi/refl_bake.h"

#ifdef __cplusplus
extern "C" {
#endif

/** GPU copy of the lm mesh set (one interleaved VBO + IBO + VAO). */
typedef struct refl_lm_set {
    uint32_t vao;
    uint32_t vbo;
    uint32_t ibo;
    uint32_t n_meshes;
    /* Per-mesh index ranges into the shared IBO (bake-time malloc). */
    uint32_t *index_offset;   /**< [n_meshes] first index. */
    uint32_t *index_count;    /**< [n_meshes] index count. */
    /* GL entry points (loaded in upload). */
    void (*glBindVertexArray)(uint32_t);
    void (*glDeleteVertexArrays)(int32_t, const uint32_t *);
    void (*glDeleteBuffers)(int32_t, const uint32_t *);
    void (*glDrawElements)(uint32_t, int32_t, uint32_t, const void *);
} refl_lm_set_t;

/**
 * Upload every opaque lm mesh (opacity >= 0.99) into one VBO/IBO pair
 * with position+normal attributes at locations 0/1. Bake-time malloc for
 * the staging + range arrays (ranges kept, staging freed). Returns false
 * on NULL args / GL failure / no meshes.
 */
bool refl_lm_upload(refl_lm_set_t *set, const gl_loader_t *loader,
                    const lm_mesh_t *meshes, uint32_t n_meshes);

/**
 * Draw every uploaded mesh with @p rb's minimal shader bound (caller sets
 * u_vp, the sun uniforms and u_ambient first): per mesh sets the tint and
 * emissive uniforms from the
 * lm tints, u_model = identity, then indexed draw.
 */
void refl_lm_draw(refl_lm_set_t *set, refl_bake_t *rb,
                  const lm_mesh_t *meshes);

/** Delete GL objects + range arrays. NULL-safe, idempotent. */
void refl_lm_destroy(refl_lm_set_t *set);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_GI_REFL_BAKE_LM_H */

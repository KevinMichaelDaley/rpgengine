#ifndef FERRUM_RENDERER_MESH_SKELETAL_MESH_H
#define FERRUM_RENDERER_MESH_SKELETAL_MESH_H

/**
 * @file skeletal_mesh.h
 * @brief Renderable geometry with per-vertex bone weights for GPU skinning.
 *
 * Extends static_mesh_t with two additional VBOs:
 *   location 6 = in_bone_weights (vec4)  — up to 4 bone influences
 *   location 7 = in_bone_indices (ivec4) — bone palette indices
 *
 * Also stores a heap-allocated array of inverse-bind matrices
 * (bone_count × mat4, row-major) used by the skinning shader to
 * transform vertices from model space into bone-local space.
 *
 * Maximum bone count: 512 (SSBO path).
 *
 * @note Ownership: skeletal_mesh_t owns all GPU resources and the
 *       inv_bind_matrices array.  Destroy via skeletal_mesh_destroy().
 * @note Nullability: all pointer parameters must be non-NULL unless
 *       documented otherwise.
 * @note Error semantics: functions return SKELETAL_MESH_OK on success
 *       or an error code.  On error, output is left unmodified.
 */

#include <stddef.h>
#include <stdint.h>

#include "ferrum/renderer/mesh/static_mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ────────────────────────────────────────────────────── */

/** Maximum bones per skeleton.  SSBO path supports up to 512. */
#define SKELETAL_MESH_MAX_BONES 512u

/** VAO attribute location for bone weights (vec4). */
#define SKELETAL_MESH_ATTR_BONE_WEIGHTS 6u

/** VAO attribute location for bone indices (ivec4). */
#define SKELETAL_MESH_ATTR_BONE_INDICES 7u

/* ── Status codes ─────────────────────────────────────────────────── */

/** @brief Status codes for skeletal mesh operations. */
typedef enum skeletal_mesh_status {
    SKELETAL_MESH_OK          = 0, /**< Success. */
    SKELETAL_MESH_ERR_INVALID = 1, /**< NULL or invalid parameter. */
    SKELETAL_MESH_ERR_GL      = 2, /**< GL resource allocation failure. */
    SKELETAL_MESH_ERR_FORMAT  = 3, /**< Bad or truncated FVMA data. */
    SKELETAL_MESH_ERR_OOM     = 4  /**< CPU-side allocation failure. */
} skeletal_mesh_status_t;

/* ── Types ────────────────────────────────────────────────────────── */

/**
 * @brief Skinned renderable mesh extending static_mesh_t.
 *
 * The base field provides all static geometry (positions, normals,
 * tangents, UVs, colors, IBO, submeshes, bounds).  Bone VBOs and
 * inverse-bind matrices are added for GPU skinning.
 */
typedef struct skeletal_mesh {
    static_mesh_t base;              /**< Inherited static mesh fields. */
    vbo_t    vbo_bone_weights;       /**< vec4 bone weights (location 6). */
    vbo_t    vbo_bone_indices;       /**< uvec4 bone indices (location 7). */
    uint32_t bone_count;             /**< Number of bones in bind pose. */
    float   *inv_bind_matrices;      /**< bone_count × mat4, row-major (heap). */
} skeletal_mesh_t;

/**
 * @brief Descriptor for creating a skeletal mesh from raw data.
 *
 * base contains the static geometry arrays (positions, normals, etc.).
 * bone_weights, bone_indices, bone_count, and inv_bind_matrices are
 * all required for a valid skeletal mesh.
 */
typedef struct skeletal_mesh_create_info {
    static_mesh_create_info_t base;          /**< Static mesh geometry. */
    const float    *bone_weights;            /**< vec4 × vertex_count (required). */
    const uint32_t *bone_indices;            /**< uvec4 × vertex_count (required). */
    uint32_t        bone_count;              /**< Number of bones (1..512). */
    const float    *inv_bind_matrices;       /**< bone_count × 16 floats (required). */
} skeletal_mesh_create_info_t;

/* ── Creation / destruction ───────────────────────────────────────── */

/**
 * @brief Create a skeletal mesh from raw attribute arrays.
 *
 * Creates the base static mesh, then uploads bone weight/index VBOs
 * at attribute locations 6 and 7, and deep-copies the inverse-bind
 * matrices.
 *
 * @param loader  GL function loader (non-NULL).
 * @param info    Creation descriptor (non-NULL, all bone fields required).
 * @param out     Output mesh (non-NULL).
 * @return Status code.
 */
int skeletal_mesh_create(const gl_loader_t *loader,
                         const skeletal_mesh_create_info_t *info,
                         skeletal_mesh_t *out);

/**
 * @brief Destroy a skeletal mesh, releasing all GPU and CPU resources.
 *
 * Destroys bone VBOs, frees inv_bind_matrices, then destroys the
 * base static mesh.  Safe to call with NULL or already-destroyed mesh.
 *
 * @param mesh  Mesh to destroy (NULL-safe).
 */
void skeletal_mesh_destroy(skeletal_mesh_t *mesh);

/* ── Drawing ──────────────────────────────────────────────────────── */

/**
 * @brief Bind the skeletal mesh VAO for drawing.
 * @param mesh  Mesh to bind (non-NULL).
 */
void skeletal_mesh_bind(const skeletal_mesh_t *mesh);

/**
 * @brief Draw a single submesh via glDrawElements.
 *
 * The mesh VAO must be bound (via skeletal_mesh_bind) before calling.
 *
 * @param mesh          Mesh (non-NULL).
 * @param submesh_index Submesh to draw (must be < submesh_count).
 */
void skeletal_mesh_draw_submesh(const skeletal_mesh_t *mesh,
                                uint32_t submesh_index);

/**
 * @brief Unbind the VAO (bind 0).
 */
void skeletal_mesh_unbind(void);

/* ── FVMA loading ─────────────────────────────────────────────────── */

/**
 * @brief Create a skeletal mesh from an FVMA binary blob with bone data.
 *
 * The FVMA must have MESH_VAO_FLAG_BONES set. Bone weights (vec4),
 * bone indices (uvec4), bone_count, and inv_bind_matrices are read
 * after the standard geometry attributes.
 *
 * @param loader    GL function loader (non-NULL).
 * @param fvma_data FVMA binary data (non-NULL).
 * @param fvma_size Size in bytes.
 * @param out       Output mesh (non-NULL).
 * @return Status code.
 */
int skeletal_mesh_create_from_fvma(const gl_loader_t *loader,
                                   const uint8_t *fvma_data,
                                   size_t fvma_size,
                                   skeletal_mesh_t *out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_MESH_SKELETAL_MESH_H */

#ifndef FERRUM_RENDERER_MESH_STATIC_MESH_H
#define FERRUM_RENDERER_MESH_STATIC_MESH_H

/**
 * @file static_mesh.h
 * @brief Immutable renderable geometry with per-attribute VBOs.
 *
 * A static mesh owns a VAO, per-attribute VBOs (position, normal, tangent,
 * UV0, UV1, color), an index buffer, and one or more submeshes.  Each
 * submesh references a material slot and a contiguous index range.
 *
 * Canonical VAO attribute locations:
 *   0 = in_position  (vec3)
 *   1 = in_normal    (vec3)
 *   2 = in_tangent   (vec4)   — optional
 *   3 = in_uv0       (vec2)   — optional
 *   4 = in_uv1       (vec2)   — optional
 *   5 = in_color     (vec4)   — optional
 *
 * Missing optional attributes bind a 1-element VBO with defaults so
 * shaders do not require permutations for attribute presence.
 *
 * @note Ownership: the static_mesh_t owns all GPU resources.
 *       Destroy via static_mesh_destroy().
 * @note Nullability: all pointer parameters must be non-NULL unless
 *       documented otherwise.
 * @note Error semantics: functions return STATIC_MESH_OK on success
 *       or an error code.  On error, output is left unmodified.
 */

#include <stddef.h>
#include <stdint.h>

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/vao.h"
#include "ferrum/renderer/vbo.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Status codes ─────────────────────────────────────────────────── */

/** @brief Status codes for static mesh operations. */
typedef enum static_mesh_status {
    STATIC_MESH_OK          = 0, /**< Success. */
    STATIC_MESH_ERR_INVALID = 1, /**< NULL or invalid parameter. */
    STATIC_MESH_ERR_GL      = 2, /**< GL resource allocation failure. */
    STATIC_MESH_ERR_FORMAT  = 3, /**< Bad or truncated FVMA data. */
    STATIC_MESH_ERR_OOM     = 4  /**< CPU-side allocation failure. */
} static_mesh_status_t;

/* ── Types ────────────────────────────────────────────────────────── */

/**
 * @brief Contiguous index range sharing one material.
 *
 * All indices in [index_offset, index_offset + index_count) reference
 * vertices in the parent mesh and are drawn with the material identified
 * by material_slot.
 */
typedef struct render_submesh {
    uint32_t index_offset;   /**< Start index (element count, not bytes). */
    uint32_t index_count;    /**< Number of indices. */
    uint16_t material_slot;  /**< Material table index. */
} render_submesh_t;

/**
 * @brief Immutable renderable geometry.
 *
 * Owns a VAO, per-attribute VBOs, an index buffer, a submesh array,
 * and precomputed bounding volumes.
 */
typedef struct static_mesh {
    vao_t    vao;             /**< Vertex array object. */
    vbo_t    vbo_position;    /**< vec3 positions   (location 0). */
    vbo_t    vbo_normal;      /**< vec3 normals     (location 1). */
    vbo_t    vbo_tangent;     /**< vec4 tangents    (location 2). */
    vbo_t    vbo_uv0;         /**< vec2 primary UVs (location 3). */
    vbo_t    vbo_uv1;         /**< vec2 lightmap UVs(location 4). */
    vbo_t    vbo_color;       /**< vec4 vertex color(location 5). */
    vbo_t    ibo;             /**< uint32 index buffer. */

    uint32_t vertex_count;    /**< Total vertices. */
    uint32_t index_count;     /**< Total indices. */

    render_submesh_t *submeshes;    /**< Heap-allocated submesh array. */
    uint32_t          submesh_count;

    float    bsphere_radius;  /**< Bounding sphere radius (origin-centered). */
    float    aabb_min[3];     /**< Axis-aligned bounding box minimum. */
    float    aabb_max[3];     /**< Axis-aligned bounding box maximum. */

    /** GL draw function pointer (resolved at creation time). */
    void (*glDrawElements)(uint32_t mode, int32_t count,
                           uint32_t type, const void *indices);
} static_mesh_t;

/**
 * @brief Descriptor for creating a static mesh from raw data.
 *
 * positions and indices are required.  All other pointers are optional;
 * when NULL, a 1-element default VBO is bound for that attribute.
 *
 * Submesh array is optional; when NULL, a single submesh covering all
 * indices with material_slot=0 is created.
 */
typedef struct static_mesh_create_info {
    const float    *positions;    /**< vec3 × vertex_count (required). */
    const float    *normals;      /**< vec3 × vertex_count (optional). */
    const float    *tangents;     /**< vec4 × vertex_count (optional). */
    const float    *uv0;          /**< vec2 × vertex_count (optional). */
    const float    *uv1;          /**< vec2 × vertex_count (optional). */
    const float    *colors;       /**< vec4 × vertex_count (optional). */
    const uint32_t *indices;      /**< Triangle indices (required). */

    uint32_t vertex_count;        /**< Number of vertices. */
    uint32_t index_count;         /**< Number of indices. */

    const render_submesh_t *submeshes;   /**< Optional submesh array. */
    uint32_t                submesh_count;
} static_mesh_create_info_t;

/* ── Creation / destruction ───────────────────────────────────────── */

/**
 * @brief Create a static mesh from raw attribute arrays.
 *
 * @param loader  GL function loader (non-NULL).
 * @param info    Creation descriptor (non-NULL, positions required).
 * @param out     Output mesh (non-NULL).
 * @return Status code.
 */
int static_mesh_create(const gl_loader_t *loader,
                       const static_mesh_create_info_t *info,
                       static_mesh_t *out);

/**
 * @brief Destroy a static mesh, releasing all GPU resources.
 *
 * Safe to call with NULL or an already-destroyed mesh.
 *
 * @param mesh  Mesh to destroy (NULL-safe).
 */
void static_mesh_destroy(static_mesh_t *mesh);

/* ── Drawing ──────────────────────────────────────────────────────── */

/**
 * @brief Bind the mesh VAO for drawing.
 * @param mesh  Mesh to bind (non-NULL).
 */
void static_mesh_bind(const static_mesh_t *mesh);

/**
 * @brief Draw a single submesh via glDrawElements.
 *
 * The mesh VAO must be bound (via static_mesh_bind) before calling.
 *
 * @param mesh          Mesh (non-NULL).
 * @param submesh_index Submesh to draw (must be < submesh_count).
 */
void static_mesh_draw_submesh(const static_mesh_t *mesh,
                              uint32_t submesh_index);

/**
 * @brief Unbind the VAO (bind 0).
 */
void static_mesh_unbind(void);

/* ── FVMA loading ─────────────────────────────────────────────────── */

/**
 * @brief Create a static mesh from an FVMA binary blob.
 *
 * Deserializes the FVMA header, reads attribute data, and uploads
 * to GPU.  A single submesh covering all indices is created.
 *
 * @param loader    GL function loader (non-NULL).
 * @param fvma_data FVMA binary data (non-NULL).
 * @param fvma_size Size in bytes.
 * @param out       Output mesh (non-NULL).
 * @return Status code.
 */
int static_mesh_create_from_fvma(const gl_loader_t *loader,
                                 const uint8_t *fvma_data,
                                 size_t fvma_size,
                                 static_mesh_t *out);

/* ── Primitive generators ─────────────────────────────────────────── */

/**
 * @brief Create a box mesh centered at origin.
 *
 * @param loader  GL loader (non-NULL).
 * @param width   Full width on X axis.
 * @param height  Full height on Y axis.
 * @param depth   Full depth on Z axis.
 * @param out     Output mesh (non-NULL).
 * @return Status code.
 */
int static_mesh_create_box(const gl_loader_t *loader,
                           float width, float height, float depth,
                           static_mesh_t *out);

/**
 * @brief Create a UV sphere mesh centered at origin.
 *
 * @param loader  GL loader (non-NULL).
 * @param radius  Sphere radius.
 * @param slices  Longitude subdivisions (≥3).
 * @param rings   Latitude subdivisions (≥2).
 * @param out     Output mesh (non-NULL).
 * @return Status code.
 */
int static_mesh_create_sphere(const gl_loader_t *loader,
                              float radius,
                              uint32_t slices, uint32_t rings,
                              static_mesh_t *out);

/**
 * @brief Create a capsule mesh (cylinder + hemisphere caps).
 *
 * @param loader       GL loader (non-NULL).
 * @param radius       Cap/cylinder radius.
 * @param half_height  Half-height of cylinder segment.
 * @param slices       Circumference subdivisions (≥3).
 * @param cap_rings    Hemisphere ring count (≥1).
 * @param out          Output mesh (non-NULL).
 * @return Status code.
 */
int static_mesh_create_capsule(const gl_loader_t *loader,
                               float radius, float half_height,
                               uint32_t slices, uint32_t cap_rings,
                               static_mesh_t *out);

/**
 * @brief Create a flat plane mesh in the XZ plane at Y=0.
 *
 * @param loader  GL loader (non-NULL).
 * @param half_w  Half-width (X axis).
 * @param half_d  Half-depth (Z axis).
 * @param out     Output mesh (non-NULL).
 * @return Status code.
 */
int static_mesh_create_plane(const gl_loader_t *loader,
                             float half_w, float half_d,
                             static_mesh_t *out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_MESH_STATIC_MESH_H */

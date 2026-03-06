#ifndef FERRUM_RENDERER_GLTF_GLTF_LOADER_H
#define FERRUM_RENDERER_GLTF_GLTF_LOADER_H

/**
 * @file gltf_loader.h
 * @brief Load glTF 2.0 / GLB files into engine mesh types.
 *
 * Parses a glTF file via cgltf, extracts geometry and skeleton data,
 * and produces static_mesh_t or skeletal_mesh_t via the existing
 * renderer mesh creation APIs.
 *
 * Supports:
 * - GLB binary container
 * - Multiple meshes per file
 * - Skinned meshes with JOINTS_0 / WEIGHTS_0 attributes
 * - Per-primitive submeshes
 * - Positions, normals, tangents, UV0, UV1, colors
 *
 * @note Ownership: the caller owns the returned gltf_scene_t and must
 *       call gltf_scene_destroy() to free it.
 * @note Nullability: all pointer parameters must be non-NULL.
 * @note Thread safety: not thread-safe.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations. */
struct gl_loader;
struct static_mesh;
struct skeletal_mesh;

/* ── Status codes ─────────────────────────────────────────────────── */

typedef enum gltf_status {
    GLTF_OK              = 0,
    GLTF_ERR_INVALID     = 1,  /**< NULL or invalid parameter. */
    GLTF_ERR_IO          = 2,  /**< File read failure. */
    GLTF_ERR_PARSE       = 3,  /**< cgltf parse/validation failure. */
    GLTF_ERR_NO_MESHES   = 4,  /**< File contains no meshes. */
    GLTF_ERR_GPU         = 5,  /**< GL resource creation failure. */
    GLTF_ERR_ALLOC       = 6,  /**< Memory allocation failure. */
} gltf_status_t;

/* ── Loaded mesh info ─────────────────────────────────────────────── */

/**
 * @brief Metadata about a single mesh within a loaded glTF scene.
 */
typedef struct gltf_mesh_info {
    char     name[128];      /**< Mesh name from glTF (truncated). */
    uint32_t vertex_count;   /**< Total vertices across all primitives. */
    uint32_t index_count;    /**< Total indices across all primitives. */
    uint32_t submesh_count;  /**< Number of primitives (submeshes). */
    int      is_skinned;     /**< Non-zero if has JOINTS_0/WEIGHTS_0. */
    int      mesh_index;     /**< Index into the glTF meshes array. */
} gltf_mesh_info_t;

/* ── Scene handle ─────────────────────────────────────────────────── */

/**
 * @brief Opaque handle to a loaded glTF scene.
 *
 * Holds the parsed cgltf data and extracted mesh metadata.
 * Created by gltf_scene_load(), destroyed by gltf_scene_destroy().
 */
typedef struct gltf_scene gltf_scene_t;

/* ── Scene API ────────────────────────────────────────────────────── */

/**
 * @brief Load and parse a glTF/GLB file.
 *
 * @param path  File path (non-NULL).
 * @param out   Receives the loaded scene handle (non-NULL).
 * @return Status code.
 */
gltf_status_t gltf_scene_load(const char *path, gltf_scene_t **out);

/**
 * @brief Destroy a loaded scene, freeing all internal resources.
 *
 * Does NOT destroy any engine meshes created from this scene.
 * Safe to call with NULL.
 *
 * @param scene  Scene to destroy (NULL-safe).
 */
void gltf_scene_destroy(gltf_scene_t *scene);

/**
 * @brief Get the number of meshes in the scene.
 *
 * @param scene  Loaded scene (non-NULL).
 * @return Mesh count, or 0 if scene is NULL.
 */
uint32_t gltf_scene_mesh_count(const gltf_scene_t *scene);

/**
 * @brief Get info about a specific mesh by index.
 *
 * @param scene  Loaded scene (non-NULL).
 * @param index  Mesh index (0..mesh_count-1).
 * @param info   Output info struct (non-NULL).
 * @return GLTF_OK on success, GLTF_ERR_INVALID if out of range.
 */
gltf_status_t gltf_scene_mesh_info(const gltf_scene_t *scene,
                                    uint32_t index,
                                    gltf_mesh_info_t *info);

/**
 * @brief Create a static_mesh_t from a glTF mesh.
 *
 * Extracts positions, normals, tangents, UVs, colors, and indices.
 * Submeshes are created from glTF primitives.
 *
 * @param scene   Loaded scene (non-NULL).
 * @param index   Mesh index (0..mesh_count-1).
 * @param loader  GL function loader (non-NULL).
 * @param out     Output mesh (non-NULL).
 * @return Status code.
 */
gltf_status_t gltf_scene_create_static_mesh(const gltf_scene_t *scene,
                                             uint32_t index,
                                             const struct gl_loader *loader,
                                             struct static_mesh *out);

/**
 * @brief Create a skeletal_mesh_t from a skinned glTF mesh.
 *
 * Extracts all static geometry plus JOINTS_0, WEIGHTS_0, and the
 * inverse bind matrices from the associated skin.
 *
 * @param scene   Loaded scene (non-NULL).
 * @param index   Mesh index (must be skinned).
 * @param loader  GL function loader (non-NULL).
 * @param out     Output mesh (non-NULL).
 * @return Status code.
 */
gltf_status_t gltf_scene_create_skeletal_mesh(const gltf_scene_t *scene,
                                               uint32_t index,
                                               const struct gl_loader *loader,
                                               struct skeletal_mesh *out);

/**
 * @brief Get the number of joints in the first skin (skeleton).
 *
 * @param scene  Loaded scene (non-NULL).
 * @return Joint count, or 0 if no skins.
 */
uint32_t gltf_scene_joint_count(const gltf_scene_t *scene);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_GLTF_GLTF_LOADER_H */

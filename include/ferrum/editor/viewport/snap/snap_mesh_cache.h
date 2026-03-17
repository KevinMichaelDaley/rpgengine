/**
 * @file snap_mesh_cache.h
 * @brief CPU-side geometry cache for surface snap raycasting.
 *
 * Retains positions, normals, and triangle indices in CPU memory
 * so the editor can raycast against entity meshes for snap-to-face,
 * snap-to-vertex, and snap-on-surface modes.
 *
 * Ownership: snap_mesh_cache_t owns all heap-allocated mesh data.
 *            Destroy via snap_mesh_cache_destroy().
 * Nullability: all pointer params must be non-NULL.
 * Error semantics: get returns NULL for missing/out-of-range entries.
 * Side effects: insert/remove allocate/free heap memory.
 *
 * Public types: snap_mesh_t, snap_mesh_cache_t (2-type rule).
 */
#ifndef FERRUM_EDITOR_VIEWPORT_SNAP_MESH_CACHE_H
#define FERRUM_EDITOR_VIEWPORT_SNAP_MESH_CACHE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* Forward declarations. */
struct mesh_slot;

/**
 * @brief CPU-side mesh geometry for snap raycasting.
 *
 * Stores positions, normals, and triangle indices. All arrays are
 * heap-allocated and owned by the snap_mesh_cache_t containing this entry.
 * An empty (unused) slot has positions == NULL.
 */
typedef struct snap_mesh {
    float    *positions;     /**< vec3 per vertex (3 * vertex_count floats). */
    float    *normals;       /**< vec3 per vertex (3 * vertex_count floats). */
    uint32_t *indices;       /**< Triangle indices (index_count entries). */
    uint32_t  vertex_count;  /**< Number of vertices. */
    uint32_t  index_count;   /**< Number of indices (multiple of 3). */
} snap_mesh_t;

/**
 * @brief Cache mapping entity_id → snap_mesh_t.
 *
 * Parallel array indexed by entity_id. Capacity matches the entity store.
 * Empty slots have positions == NULL.
 */
typedef struct snap_mesh_cache {
    snap_mesh_t *meshes;     /**< Array of snap meshes [capacity]. */
    uint32_t     capacity;   /**< Maximum entity_id + 1. */
} snap_mesh_cache_t;

/* ---- Lifecycle (snap_mesh_cache.c) ---- */

/**
 * @brief Initialize snap mesh cache with given capacity.
 *
 * Allocates the meshes array and zeros all slots.
 *
 * @param cache    Cache to initialize (non-NULL).
 * @param capacity Maximum number of entity slots.
 */
void snap_mesh_cache_init(snap_mesh_cache_t *cache, uint32_t capacity);

/**
 * @brief Destroy snap mesh cache, freeing all retained mesh data.
 *
 * @param cache  Cache to destroy (non-NULL).
 */
void snap_mesh_cache_destroy(snap_mesh_cache_t *cache);

/**
 * @brief Insert (or replace) snap mesh data for an entity.
 *
 * Copies positions, normals, and indices into heap-allocated buffers.
 * If the slot already has data, it is freed first.
 *
 * @param cache       Cache (non-NULL).
 * @param entity_id   Entity ID (must be < capacity).
 * @param positions   Position data (vec3 × vertex_count, non-NULL).
 * @param normals     Normal data (vec3 × vertex_count, non-NULL).
 * @param indices     Index data (index_count entries, non-NULL).
 * @param vertex_count Number of vertices.
 * @param index_count  Number of indices.
 */
void snap_mesh_cache_insert(snap_mesh_cache_t *cache, uint32_t entity_id,
                             const float *positions, const float *normals,
                             const uint32_t *indices,
                             uint32_t vertex_count, uint32_t index_count);

/**
 * @brief Remove snap mesh data for an entity, freeing its buffers.
 *
 * Safe to call if the slot is already empty or entity_id is out of range.
 *
 * @param cache      Cache (non-NULL).
 * @param entity_id  Entity ID.
 */
void snap_mesh_cache_remove(snap_mesh_cache_t *cache, uint32_t entity_id);

/* ---- Query (snap_mesh_cache_query.c) ---- */

/**
 * @brief Get a const pointer to the snap mesh for an entity.
 *
 * @param cache      Cache (non-NULL).
 * @param entity_id  Entity ID.
 * @return Pointer to snap_mesh_t, or NULL if not cached or out of range.
 */
const snap_mesh_t *snap_mesh_cache_get(const snap_mesh_cache_t *cache,
                                        uint32_t entity_id);

/**
 * @brief Check whether snap mesh data exists for an entity.
 *
 * @param cache      Cache (non-NULL).
 * @param entity_id  Entity ID.
 * @return true if the entity has cached snap mesh data.
 */
bool snap_mesh_cache_has(const snap_mesh_cache_t *cache, uint32_t entity_id);

/* ---- Retain (snap_mesh_retain.c) ---- */

/**
 * @brief Copy CPU-side geometry from a mesh_slot into the snap cache.
 *
 * Called during FVMA mesh loading, before mesh_slot_destroy() frees
 * the CPU data. Copies positions, normals, and indices.
 *
 * @param cache      Cache (non-NULL).
 * @param entity_id  Entity ID (must be < capacity).
 * @param slot       Mesh slot with CPU-side data (non-NULL).
 */
void snap_mesh_retain_from_slot(snap_mesh_cache_t *cache,
                                 uint32_t entity_id,
                                 const struct mesh_slot *slot);

/**
 * @brief Generate and cache snap mesh for a unit box primitive.
 *
 * Creates a unit box (half-extent 0.5) with 24 vertices and 36 indices.
 * Entity transforms (scale) are applied via the model matrix at raycast time.
 *
 * @param cache      Cache (non-NULL).
 * @param entity_id  Entity ID (must be < capacity).
 */
void snap_mesh_retain_box(snap_mesh_cache_t *cache, uint32_t entity_id);

/* ---- Retain primitives (snap_mesh_retain_prim.c) ---- */

/**
 * @brief Generate and cache snap mesh for a unit sphere primitive.
 *
 * Creates a UV sphere (radius 0.5) with 16×16 segments.
 * Entity transforms (scale) are applied via the model matrix at raycast time.
 *
 * @param cache      Cache (non-NULL).
 * @param entity_id  Entity ID (must be < capacity).
 */
void snap_mesh_retain_sphere(snap_mesh_cache_t *cache, uint32_t entity_id);

/**
 * @brief Generate and cache snap mesh for a capsule primitive.
 *
 * Creates a capsule (radius 0.3, half_height 0.5, total height 1.6)
 * matching the rendering capsule in scene_viewport_shaders.c.
 * Entity transforms (scale) are applied via the model matrix at raycast time.
 *
 * @param cache      Cache (non-NULL).
 * @param entity_id  Entity ID (must be < capacity).
 */
void snap_mesh_retain_capsule(snap_mesh_cache_t *cache, uint32_t entity_id);


/* ---- Retain convex (snap_mesh_retain_convex.c) ---- */

struct phys_convex_hull;
struct phys_convex_compound;

/**
 * @brief Generate and cache snap mesh from a convex hull.
 *
 * Fan-triangulates the hull's polygon faces and inserts the resulting
 * triangle mesh into the cache. Per-vertex normals are averaged from
 * adjacent face normals.
 *
 * @param cache      Cache (non-NULL).
 * @param entity_id  Entity ID (must be < capacity).
 * @param hull       Convex hull (non-NULL, at least 4 verts/faces).
 */
void snap_mesh_retain_convex_hull(snap_mesh_cache_t *cache,
                                    uint32_t entity_id,
                                    const struct phys_convex_hull *hull);

/**
 * @brief Generate and cache snap mesh from a compound collider.
 *
 * Merges all child hulls into a single triangulated snap mesh.
 * Each child hull's polygon faces are fan-triangulated and the
 * vertices are concatenated with offset indices.
 *
 * @param cache      Cache (non-NULL).
 * @param entity_id  Entity ID (must be < capacity).
 * @param hulls      Hull pool array (indexed by compound's child indices).
 * @param compound   Compound collider with child hull indices (non-NULL).
 */
void snap_mesh_retain_compound(snap_mesh_cache_t *cache,
                                 uint32_t entity_id,
                                 const struct phys_convex_hull *hulls,
                                 const struct phys_convex_compound *compound);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_VIEWPORT_SNAP_MESH_CACHE_H */

/**
 * @file mesh_uv_seam.h
 * @brief UV seam marking on mesh edges.
 *
 * Types: mesh_seam_set_t (edge seam storage).
 *
 * Seams are stored as canonical vertex pairs (v0 < v1) in a compact
 * array. Edges on seams split UV coordinates during unwrapping.
 *
 * Ownership: mesh_seam_set_t owns its internal buffer.
 * Nullability: NULL pointers handled gracefully.
 * Thread safety: not thread-safe.
 */
#ifndef FERRUM_EDITOR_MESH_UV_SEAM_H
#define FERRUM_EDITOR_MESH_UV_SEAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------------ */
/* Constants                                                                 */
/* ------------------------------------------------------------------------ */

/** @brief Maximum number of seam edges. */
#define MESH_SEAM_MAX 4096

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief Set of seam edges stored as canonical vertex pairs.
 *
 * Each edge is stored as (v0, v1) where v0 < v1.
 */
typedef struct mesh_seam_set {
    uint32_t v0[MESH_SEAM_MAX]; /**< First vertex of each seam edge. */
    uint32_t v1[MESH_SEAM_MAX]; /**< Second vertex of each seam edge. */
    uint32_t count;             /**< Number of seam edges. */
} mesh_seam_set_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize a seam set to empty.
 * @param set  Set to initialize. Not NULL.
 */
void mesh_seam_set_init(mesh_seam_set_t *set);

/**
 * @brief Destroy a seam set (zeroes it).
 * @param set  Set to destroy. NULL is safe.
 */
void mesh_seam_set_destroy(mesh_seam_set_t *set);

/**
 * @brief Remove all seam edges.
 * @param set  Set to clear. NULL is safe.
 */
void mesh_seam_set_clear_all(mesh_seam_set_t *set);

/* ------------------------------------------------------------------------ */
/* Operations                                                                */
/* ------------------------------------------------------------------------ */

/**
 * @brief Mark an edge as a UV seam.
 *
 * Order-independent: (a,b) is the same as (b,a).
 *
 * @param set  Seam set. Not NULL.
 * @param a    First vertex index.
 * @param b    Second vertex index.
 * @return true on success, false if NULL or set full.
 */
bool mesh_seam_mark(mesh_seam_set_t *set, uint32_t a, uint32_t b);

/**
 * @brief Clear seam marking from an edge.
 *
 * @param set  Seam set. NULL is safe.
 * @param a    First vertex index.
 * @param b    Second vertex index.
 */
void mesh_seam_clear(mesh_seam_set_t *set, uint32_t a, uint32_t b);

/**
 * @brief Check if an edge is marked as a seam.
 *
 * @param set  Seam set. NULL returns false.
 * @param a    First vertex index.
 * @param b    Second vertex index.
 * @return true if marked.
 */
bool mesh_seam_is_marked(const mesh_seam_set_t *set, uint32_t a, uint32_t b);

/**
 * @brief Number of marked seam edges.
 *
 * @param set  Seam set. NULL returns 0.
 * @return Seam count.
 */
uint32_t mesh_seam_count(const mesh_seam_set_t *set);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_MESH_UV_SEAM_H */

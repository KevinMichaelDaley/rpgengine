/**
 * @file mesh_material.h
 * @brief Per-face material assignment via polygroup→material mapping.
 *
 * Types: mesh_material_map_t (polygroup-to-material path table).
 *
 * Each entry maps a polygroup ID (uint16) to a material path string.
 * Assigning a material to selected faces sets their polygroup_ids to
 * the polygroup for that material, creating a new polygroup if needed.
 *
 * Ownership: mesh_material_map_t owns its string storage.
 * Nullability: NULL pointers handled gracefully.
 * Thread safety: not thread-safe.
 */
#ifndef FERRUM_EDITOR_MESH_MATERIAL_H
#define FERRUM_EDITOR_MESH_MATERIAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ferrum/editor/mesh/mesh_slot.h"
#include "ferrum/editor/mesh/mesh_edit.h"

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------------ */
/* Constants                                                                 */
/* ------------------------------------------------------------------------ */

/** @brief Maximum material slots in a single map. */
#define MESH_MATERIAL_MAP_MAX 64

/** @brief Maximum material path length (including NUL). */
#define MESH_MATERIAL_PATH_MAX 256

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief Polygroup→material path mapping table.
 *
 * Fixed-capacity table mapping polygroup IDs to material path strings.
 * Entries are compacted — entry i maps polygroup_ids[i] → paths[i].
 */
typedef struct mesh_material_map {
    uint16_t polygroup_ids[MESH_MATERIAL_MAP_MAX]; /**< Polygroup IDs. */
    char     paths[MESH_MATERIAL_MAP_MAX][MESH_MATERIAL_PATH_MAX]; /**< Material paths. */
    uint32_t count; /**< Number of active entries. */
} mesh_material_map_t;

/* ------------------------------------------------------------------------ */
/* Map lifecycle (mesh_material.c)                                           */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize a material map to empty.
 *
 * @param map  Map to initialize. Not NULL.
 */
void mesh_material_map_init(mesh_material_map_t *map);

/**
 * @brief Destroy a material map (no-op for stack struct, but zeroes it).
 *
 * @param map  Map to destroy. NULL is safe.
 */
void mesh_material_map_destroy(mesh_material_map_t *map);

/**
 * @brief Look up material path for a polygroup ID.
 *
 * @param map  Map. NULL returns NULL.
 * @param pg   Polygroup ID.
 * @return Material path string, or NULL if not mapped.
 */
const char *mesh_material_map_get(const mesh_material_map_t *map, uint16_t pg);

/**
 * @brief Number of active polygroup→material entries.
 *
 * @param map  Map. NULL returns 0.
 * @return Entry count.
 */
uint32_t mesh_material_map_count(const mesh_material_map_t *map);

/* ------------------------------------------------------------------------ */
/* Material assignment (mesh_material_assign.c)                              */
/* ------------------------------------------------------------------------ */

/**
 * @brief Assign a material to selected faces.
 *
 * Sets polygroup_ids of all selected faces to the polygroup mapped to
 * @p material_path. If the path already exists in @p map, reuses that
 * polygroup. Otherwise allocates a new polygroup ID.
 *
 * @param slot           Mesh. Not NULL.
 * @param map            Material map. Not NULL.
 * @param sel            Face selection. Not NULL.
 * @param material_path  Material path string. Not NULL.
 * @return true on success, false on NULL or map full.
 */
bool mesh_material_assign(mesh_slot_t *slot, mesh_material_map_t *map,
                          const mesh_sel_bitset_t *sel,
                          const char *material_path);

/**
 * @brief Get the material path for a specific face.
 *
 * Convenience: looks up polygroup_ids[face_index] in @p map.
 *
 * @param slot  Mesh. Not NULL.
 * @param map   Material map. Not NULL.
 * @param face  Face index.
 * @return Material path, or NULL if face is out of bounds or unmapped.
 */
const char *mesh_material_get_face(const mesh_slot_t *slot,
                                   const mesh_material_map_t *map,
                                   uint32_t face);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_MESH_MATERIAL_H */

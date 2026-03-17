/**
 * @file scene_asset_load.h
 * @brief Scene editor asset loading: FVMA mesh + fskel skeleton from disk.
 *
 * Reads asset files from the asset directory and loads them into the
 * editor's viewport mesh registry or skeleton storage.
 *
 * Ownership: loaded GPU meshes are owned by the viewport's mesh_registry_t.
 * Nullability: all pointer params must be non-NULL.
 * Error semantics: returns false on file I/O or format error.
 * Side effects: allocates heap memory for file reading; creates GPU resources.
 *
 * Public types: none.
 */
#ifndef FERRUM_EDITOR_SCENE_ASSET_LOAD_H
#define FERRUM_EDITOR_SCENE_ASSET_LOAD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* Forward declarations. */
struct scene_editor;

/**
 * @brief Load an FVMA mesh file and register it for an entity.
 *
 * Reads the FVMA binary from disk, uploads to GPU via the viewport's
 * mesh registry, and caches the handle by entity_id.
 *
 * @param ed          Scene editor context.
 * @param entity_id   Entity to associate the mesh with.
 * @param asset_path  Path relative to asset root (e.g., "humanoid.fvma").
 * @return true on success, false on file or format error.
 */
bool scene_load_entity_mesh(struct scene_editor *ed, uint32_t entity_id,
                            const char *asset_path);

/**
 * @brief Load all meshes for MESH-type entities that have a mesh_path attr.
 *
 * Scans the entity store for MESH entities with SCRIPT_KEY_MESH_PATH
 * attributes and loads any that aren't already cached in the viewport.
 *
 * @param ed  Scene editor context.
 * @return Number of meshes loaded.
 */
uint32_t scene_load_pending_meshes(struct scene_editor *ed);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_ASSET_LOAD_H */

/**
 * @file scene_desc_object.h
 * @brief One renderable object entry in a scene/level descriptor (rpg-51nf).
 *
 * A borrowed-path, transform, material-index, and baked-lightmap mapping for a
 * single mesh instance. Descriptors keep objects in a fixed array whose ORDER is
 * significant: it is the lightmap bake order (the per-mesh sh_layer/atlas-rect
 * mapping is indexed by it), so loaders MUST preserve it verbatim.
 *
 * Pure data (no GL, no ownership of the referenced assets). Consumed by the
 * render-world builder (client) and the server level loader.
 */
#ifndef FERRUM_SCENE_SCENE_DESC_OBJECT_H
#define FERRUM_SCENE_SCENE_DESC_OBJECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** Max material slots referenced by one object (submesh materials). */
/* Per-object material slots. The LA generator alone uses 19 (its shared _MATS
 * palette: stucco..soil), and a face's polygroup id indexes this list -- an 8
 * cap truncated everything from the signs (slot 8) onward, collapsing them all
 * to material 0 (grey signs, brown roads). Keep comfortable headroom. */
#define SCENE_DESC_MAX_OBJ_MATERIALS 32u
/** Fixed capacity for the object name string (incl. null terminator). */
#define SCENE_DESC_OBJ_NAME_CAP 64u
/** Fixed capacity for the mesh asset path (incl. null terminator). */
#define SCENE_DESC_PATH_CAP 192u

/**
 * @brief A single mesh instance in the scene descriptor.
 *
 * Nullability: all fields are value types; strings are always null-terminated.
 * @c material_idx entries index into the owning scene_desc_t::materials table;
 * @c sh_layer is the baked-SH atlas layer (-1 = not baked / lit only at runtime).
 */
typedef struct scene_desc_object {
    char     name[SCENE_DESC_OBJ_NAME_CAP]; /**< object identifier. */
    char     mesh[SCENE_DESC_PATH_CAP];     /**< relative asset path (fvma/glb/dmesh/obj). */
    char     skeleton[SCENE_DESC_PATH_CAP]; /**< skinning skeleton (.fskel); empty = static mesh. */
    float    position[3];                   /**< world translation. */
    float    rotation[4];                   /**< orientation quaternion (x,y,z,w). */
    float    scale[3];                      /**< per-axis scale. */
    uint32_t material_count;                /**< entries used in material_idx. */
    int32_t  material_idx[SCENE_DESC_MAX_OBJ_MATERIALS]; /**< into scene_desc materials[]; -1 = unresolved. */
    int32_t  lightmap_res;                  /**< baked luxel resolution hint (0 = none). */
    int32_t  sh_layer;                      /**< baked-SH atlas layer (-1 = no bake). */
    int32_t  dynamic;                       /**< 1 = DYNAMIC: excluded from the offline
                                             *   bake (no lightmap slot, absent from the
                                             *   baked voxel albedo); the runtime
                                             *   voxelises it into the dynamic albedo
                                             *   volume so its colour still bleeds. */
    int32_t  building;                      /**< 1 = BUILDING (ferrum_building on the
                                             *   Blender object): the offline probe
                                             *   placer densifies a probe shell around
                                             *   this object's surfaces (interior rooms
                                             *   + exterior facades) for crisp GI. */
} scene_desc_object_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_SCENE_SCENE_DESC_OBJECT_H */

/**
 * @file gi_voxelize.h
 * @brief GPU voxelisation of DYNAMIC geometry into the sparse albedo volume (rpg-3c6g).
 *
 * Dynamic objects are excluded from the offline bake, so the probe GI would only
 * ever see them as occluders and bounce a neutral grey. This rasterises their real
 * triangles into the RGBA8 dynamic albedo volume each probe update, tinted by the
 * object's material albedo, so their colour bleeds (a red cloth banner bleeds RED).
 *
 * Method: rasterise the mesh under an axis-aligned orthographic projection and let
 * the FRAGMENT shader imageStore() the albedo at the fragment's world->voxel coord.
 * Writing through an image (not a colour attachment) means the destination voxel is
 * computed from the world position, so no gl_Layer routing is needed and the SAME
 * pass can be repeated along all three axes -- which is what gives hole-free
 * coverage for surfaces of any orientation (a plane perpendicular to one projection
 * is fully covered by another). Depth test/write are off so every surface along a
 * ray is captured, not just the nearest.
 *
 * Ownership: owns its program + framebuffer; the volume texture is borrowed.
 * GL 4.3 (image load/store + attachment-less FBO). Render thread only.
 */
#ifndef FERRUM_RENDERER_GI_VOXELIZE_H
#define FERRUM_RENDERER_GI_VOXELIZE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/mesh/static_mesh.h"

/** GPU voxeliser state (program + attachment-less FBO + cached uniform slots). */
typedef struct gi_voxelize {
    unsigned int prog;      /**< voxelise program (0 = not built). */
    unsigned int fbo;       /**< attachment-less FBO used purely for rasterisation. */
    int loc_model, loc_origin, loc_extent, loc_dim, loc_albedo, loc_axis, loc_vol;
    int dim[3];             /**< volume dims for the pass in flight. */
    bool ready;
    /* GL entry points not in the 3.3 core loader. */
    void (*BindImageTexture)(unsigned int, unsigned int, int, unsigned char, int,
                             unsigned int, unsigned int);
    void (*MemoryBarrier)(unsigned int);
    void (*FramebufferParameteri)(unsigned int, unsigned int, int);
    void (*GenFramebuffers)(int, unsigned int *);
    void (*DeleteFramebuffers)(int, const unsigned int *);
    void (*BindFramebuffer)(unsigned int, unsigned int);
} gi_voxelize_t;

/** @brief Build the program + FBO. @return false if GL 4.3 entry points are absent. */
bool gi_voxelize_init(gi_voxelize_t *v, const gl_loader_t *loader);

/** @brief Free owned GL objects. NULL-safe. */
void gi_voxelize_destroy(gi_voxelize_t *v);

/**
 * @brief Begin a voxelisation pass into @p vol_tex (RGBA8 image3D) covering the
 *        world box @p origin .. @p origin+@p extent at @p dim voxels. Binds the
 *        image, disables depth/colour writes and sets the raster viewport.
 */
void gi_voxelize_begin(gi_voxelize_t *v, unsigned int vol_tex, const int dim[3],
                       const float origin[3], const float extent[3]);

/**
 * @brief Rasterise one mesh (world transform @p model) into the bound volume with
 *        flat @p albedo, along all three axes. Call between begin/end.
 */
void gi_voxelize_mesh(gi_voxelize_t *v, const static_mesh_t *mesh,
                      const float model[16], const float albedo[3]);

/** @brief End the pass: restore GL state and barrier the image writes. */
void gi_voxelize_end(gi_voxelize_t *v);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_GI_VOXELIZE_H */

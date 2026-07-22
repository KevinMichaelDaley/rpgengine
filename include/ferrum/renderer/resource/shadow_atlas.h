/**
 * @file shadow_atlas.h
 * @brief Resource-managed shadow depth-target array: a GL 2D-array texture whose
 *        layers are handed out per-light by a @ref shadow_slotmap and whose
 *        texture is tracked as a @ref gpu_resource in the registry.
 *
 * This is the shared home for shadow depth targets (rpg-t2xr): a stationary
 * light claims a contiguous layer run (N static cascades, or 1 dynamic map) at a
 * configured resolution/format. The atlas owns the array texture, an FBO, and a
 * shared depth renderbuffer for rendering into any layer. GL is created at init
 * on the render thread; per-frame use is layer bind + draw (no allocation).
 *
 * Ownership: owns the GL texture/FBO/renderbuffer and the registry handle for
 * the texture; frees them in @ref shadow_atlas_destroy.
 */
#ifndef FERRUM_RENDERER_RESOURCE_SHADOW_ATLAS_H
#define FERRUM_RENDERER_RESOURCE_SHADOW_ATLAS_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/resource/gpu_registry.h"
#include "ferrum/renderer/resource/shadow_slotmap.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Hard cap on layers (keeps the slotmap backing inline / allocation-free). */
#define SHADOW_ATLAS_MAX_LAYERS 64u

/** Setup for @ref shadow_atlas_init (all by value / borrowed). */
typedef struct shadow_atlas_config {
    const gl_loader_t *loader;
    gpu_registry_t    *registry;   /**< tracks the array texture as a resource. */
    uint32_t           resolution; /**< per-layer resolution. */
    uint32_t           layers;     /**< total layers (<= SHADOW_ATLAS_MAX_LAYERS). */
    uint32_t           internal_format; /**< e.g. GL_RG32F (EVSM) or GL_R32F. */
    bool               nearest;    /**< true = GL_NEAREST (point) sampling. Use for
                                    *   discontinuous data (translucency coverage /
                                    *   distance masks) where bilinear blending toward
                                    *   the clear value erodes edges; EVSM moment maps
                                    *   stay bilinear (false, the default). */
} shadow_atlas_config_t;

/** A managed shadow depth-target array + its slot allocator. */
typedef struct shadow_atlas {
    uint32_t texture;    /**< GL 2D-array. */
    uint32_t fbo;
    uint32_t depth_rb;   /**< shared depth renderbuffer (one layer at a time). */
    uint32_t resolution;
    uint32_t layers;
    uint32_t format;
    uint64_t resource;   /**< registry handle for @c texture. */
    shadow_slotmap_t slots;
    uint8_t  slot_backing[SHADOW_ATLAS_MAX_LAYERS];

    void (*glGenTextures)(int32_t, uint32_t *);
    void (*glDeleteTextures)(int32_t, const uint32_t *);
    void (*glBindTexture)(uint32_t, uint32_t);
    void (*glActiveTexture)(uint32_t);
    void (*glTexImage3D)(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, const void *);
    void (*glTexParameteri)(uint32_t, uint32_t, int32_t);
    void (*glGenRenderbuffers)(int32_t, uint32_t *);
    void (*glDeleteRenderbuffers)(int32_t, const uint32_t *);
    void (*glBindRenderbuffer)(uint32_t, uint32_t);
    void (*glRenderbufferStorage)(uint32_t, uint32_t, int32_t, int32_t);
    void (*glGenFramebuffers)(int32_t, uint32_t *);
    void (*glDeleteFramebuffers)(int32_t, const uint32_t *);
    void (*glBindFramebuffer)(uint32_t, uint32_t);
    void (*glFramebufferTextureLayer)(uint32_t, uint32_t, uint32_t, int32_t, int32_t);
    void (*glFramebufferRenderbuffer)(uint32_t, uint32_t, uint32_t, uint32_t);
} shadow_atlas_t;

/** @brief Create the array texture + FBO + depth RB and register the texture. */
bool shadow_atlas_init(shadow_atlas_t *atlas, const shadow_atlas_config_t *config);

/**
 * @brief Reserve @p count contiguous layers for a light. Returns the base layer
 *        index or -1 if none fit.
 */
int32_t shadow_atlas_alloc(shadow_atlas_t *atlas, uint32_t count);

/** @brief Release a previously allocated layer run. */
void shadow_atlas_free(shadow_atlas_t *atlas, uint32_t base, uint32_t count);

/**
 * @brief Bind the atlas FBO with @p layer attached as colour + the shared depth
 *        RB, ready to render that layer (caller sets viewport/clears/draws).
 */
void shadow_atlas_bind_layer(shadow_atlas_t *atlas, uint32_t layer);

/** @brief Bind the array texture to @p unit for sampling. */
void shadow_atlas_bind_sample(const shadow_atlas_t *atlas, uint32_t unit);

/** @brief Release all GL resources + the registry handle. NULL-safe. */
void shadow_atlas_destroy(shadow_atlas_t *atlas);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_RESOURCE_SHADOW_ATLAS_H */

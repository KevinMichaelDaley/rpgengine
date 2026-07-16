/**
 * @file shadow_atlas.c
 * @brief Resource-managed shadow depth-target array (see shadow_atlas.h).
 */
#include "ferrum/renderer/resource/shadow_atlas.h"

#include <stddef.h>
#include <string.h>

#include "ferrum/renderer/gl_constants.h"

#define SA_LOAD(dst, name)                                                    \
    do {                                                                      \
        void *p_ = loader->get_proc_address((name), loader->user_data);       \
        if (p_ == NULL) return false;                                         \
        memcpy(&(dst), &p_, sizeof(p_));                                      \
    } while (0)

bool shadow_atlas_init(shadow_atlas_t *atlas, const shadow_atlas_config_t *config)
{
    if (atlas == NULL || config == NULL || config->loader == NULL ||
        config->loader->get_proc_address == NULL || config->registry == NULL ||
        config->resolution == 0u || config->layers == 0u ||
        config->layers > SHADOW_ATLAS_MAX_LAYERS)
        return false;
    const gl_loader_t *loader = config->loader;
    memset(atlas, 0, sizeof(*atlas));
    atlas->resolution = config->resolution;
    atlas->layers = config->layers;
    atlas->format = config->internal_format;
    shadow_slotmap_init(&atlas->slots, atlas->slot_backing, config->layers);

    SA_LOAD(atlas->glGenTextures, "glGenTextures");
    SA_LOAD(atlas->glDeleteTextures, "glDeleteTextures");
    SA_LOAD(atlas->glBindTexture, "glBindTexture");
    SA_LOAD(atlas->glActiveTexture, "glActiveTexture");
    SA_LOAD(atlas->glTexImage3D, "glTexImage3D");
    SA_LOAD(atlas->glTexParameteri, "glTexParameteri");
    SA_LOAD(atlas->glGenRenderbuffers, "glGenRenderbuffers");
    SA_LOAD(atlas->glDeleteRenderbuffers, "glDeleteRenderbuffers");
    SA_LOAD(atlas->glBindRenderbuffer, "glBindRenderbuffer");
    SA_LOAD(atlas->glRenderbufferStorage, "glRenderbufferStorage");
    SA_LOAD(atlas->glGenFramebuffers, "glGenFramebuffers");
    SA_LOAD(atlas->glDeleteFramebuffers, "glDeleteFramebuffers");
    SA_LOAD(atlas->glBindFramebuffer, "glBindFramebuffer");
    SA_LOAD(atlas->glFramebufferTextureLayer, "glFramebufferTextureLayer");
    SA_LOAD(atlas->glFramebufferRenderbuffer, "glFramebufferRenderbuffer");

    /* Array texture: pick a sane external format for the storage-only upload. */
    uint32_t ext = (config->internal_format == GL_R32F) ? GL_RED : GL_RG;
    atlas->glGenTextures(1, &atlas->texture);
    atlas->glBindTexture(GL_TEXTURE_2D_ARRAY, atlas->texture);
    atlas->glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, (int32_t)config->internal_format,
                        (int32_t)config->resolution, (int32_t)config->resolution,
                        (int32_t)config->layers, 0, ext, GL_FLOAT, NULL);
    /* Trilinear: the EVSM moments are mipmapped (shadow_atlas holds the sun's
     * variance cascades) so a receiver can sample a coarser level for a soft,
     * variable-width penumbra. Mips are (re)generated after each static bake. */
    atlas->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER,
                           GL_LINEAR_MIPMAP_LINEAR);
    atlas->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    atlas->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    atlas->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    atlas->glGenRenderbuffers(1, &atlas->depth_rb);
    atlas->glBindRenderbuffer(GL_RENDERBUFFER, atlas->depth_rb);
    atlas->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                                 (int32_t)config->resolution, (int32_t)config->resolution);
    atlas->glGenFramebuffers(1, &atlas->fbo);
    atlas->glBindFramebuffer(GL_FRAMEBUFFER, 0);

    /* Track the array texture as a GPU resource (live, render-thread-created). */
    atlas->resource = gpu_registry_alloc(config->registry, GPU_RESOURCE_SHADOW_TARGET);
    gpu_resource_t *r = gpu_registry_get(config->registry, atlas->resource);
    if (r != NULL) {
        r->gl_name = atlas->texture;
        r->width = r->height = config->resolution;
        r->layers = config->layers;
        r->format = config->internal_format;
        atomic_store_explicit(&r->ready, 1, memory_order_release);
    }
    return true;
}

int32_t shadow_atlas_alloc(shadow_atlas_t *atlas, uint32_t count)
{
    if (atlas == NULL)
        return -1;
    return shadow_slotmap_alloc(&atlas->slots, count);
}

void shadow_atlas_free(shadow_atlas_t *atlas, uint32_t base, uint32_t count)
{
    if (atlas != NULL)
        shadow_slotmap_free(&atlas->slots, base, count);
}

void shadow_atlas_bind_layer(shadow_atlas_t *atlas, uint32_t layer)
{
    if (atlas == NULL || layer >= atlas->layers)
        return;
    atlas->glBindFramebuffer(GL_FRAMEBUFFER, atlas->fbo);
    atlas->glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                     atlas->texture, 0, (int32_t)layer);
    atlas->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                     GL_RENDERBUFFER, atlas->depth_rb);
}

void shadow_atlas_bind_sample(const shadow_atlas_t *atlas, uint32_t unit)
{
    if (atlas == NULL)
        return;
    atlas->glActiveTexture(GL_TEXTURE0 + unit);
    atlas->glBindTexture(GL_TEXTURE_2D_ARRAY, atlas->texture);
}

void shadow_atlas_destroy(shadow_atlas_t *atlas)
{
    if (atlas == NULL)
        return;
    if (atlas->glDeleteFramebuffers && atlas->fbo)
        atlas->glDeleteFramebuffers(1, &atlas->fbo);
    if (atlas->glDeleteRenderbuffers && atlas->depth_rb)
        atlas->glDeleteRenderbuffers(1, &atlas->depth_rb);
    if (atlas->glDeleteTextures && atlas->texture)
        atlas->glDeleteTextures(1, &atlas->texture);
    atlas->fbo = atlas->depth_rb = atlas->texture = 0;
}

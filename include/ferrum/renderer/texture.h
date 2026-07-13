#ifndef FERRUM_RENDERER_TEXTURE_H
#define FERRUM_RENDERER_TEXTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/texture_format.h"

/** @file
 * @brief 2D GL texture wrapper with explicit ownership and format handling.
 *
 * Mirrors the vbo/vao wrappers: each texture holds its own GL entry points
 * resolved from a gl_loader_t at create time (no global GL symbols). Upload a
 * 2D image with a @ref texture_format (sRGB vs linear chosen by the format),
 * optionally generating mips; set sampler state; bind to a texture unit for a
 * draw. Ownership of the GL object is explicit: destroy releases it.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Status codes for texture operations. */
typedef enum texture_status {
    TEXTURE_OK = 0,
    TEXTURE_ERR_INVALID = 1,    /**< NULL texture or zero dimensions. */
    TEXTURE_ERR_MISSING_GL = 2, /**< A required GL entry point was absent. */
    TEXTURE_ERR_FORMAT = 3      /**< Unknown texture_format. */
} texture_status_t;

/** 2D texture wrapper. GL entry points are bound at create time. */
typedef struct texture {
    uint32_t handle; /**< GL texture object name (0 when invalid). */
    uint32_t target; /**< Texture target (GL_TEXTURE_2D). */
    void (*glGenTextures)(int32_t n, uint32_t *textures);
    void (*glDeleteTextures)(int32_t n, const uint32_t *textures);
    void (*glBindTexture)(uint32_t target, uint32_t texture);
    void (*glActiveTexture)(uint32_t texture_unit);
    void (*glTexImage2D)(uint32_t target, int32_t level, int32_t internalformat,
                         int32_t width, int32_t height, int32_t border,
                         uint32_t format, uint32_t type, const void *pixels);
    void (*glTexParameteri)(uint32_t target, uint32_t pname, int32_t param);
    void (*glGenerateMipmap)(uint32_t target);
} texture_t;

/**
 * @brief Create a 2D texture object.
 * @param tex    Output texture (non-NULL); zeroed on entry.
 * @param loader GL loader table (non-NULL).
 * @return TEXTURE_OK, or an error if @p tex/loader are invalid or GL is missing.
 */
texture_status_t texture_create(texture_t *tex, const gl_loader_t *loader);

/**
 * @brief Upload a 2D image, replacing the texture's contents at level 0.
 * @param tex       Texture created by texture_create.
 * @param format    Pixel format (selects sRGB vs linear internal format).
 * @param width     Width in texels (> 0).
 * @param height    Height in texels (> 0).
 * @param pixels    Source pixels (may be NULL to allocate storage only).
 * @param gen_mips  Generate a full mip chain after upload.
 * @return TEXTURE_OK or an error code.
 */
texture_status_t texture_upload_2d(texture_t *tex, texture_format_t format,
                                   uint32_t width, uint32_t height,
                                   const void *pixels, bool gen_mips);

/**
 * @brief Set the texture's sampler state (filtering + wrap on S and T).
 * @param tex        Texture created by texture_create.
 * @param min_filter GL minification filter (e.g. GL_LINEAR_MIPMAP_LINEAR).
 * @param mag_filter GL magnification filter (e.g. GL_LINEAR).
 * @param wrap_s     GL wrap mode for S (e.g. GL_REPEAT).
 * @param wrap_t     GL wrap mode for T.
 * @return TEXTURE_OK or an error code.
 */
texture_status_t texture_set_sampler(texture_t *tex, uint32_t min_filter,
                                     uint32_t mag_filter, uint32_t wrap_s,
                                     uint32_t wrap_t);

/**
 * @brief Bind the texture to a texture unit (0-based).
 * @param tex  Texture created by texture_create.
 * @param unit Texture unit index (0 => GL_TEXTURE0).
 * @return TEXTURE_OK or an error code.
 */
texture_status_t texture_bind(const texture_t *tex, uint32_t unit);

/**
 * @brief Destroy the texture, releasing its GL object. NULL-safe.
 */
void texture_destroy(texture_t *tex);

/**
 * @brief Retrieve the underlying GL texture handle (0 if invalid).
 */
uint32_t texture_handle(const texture_t *tex);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_TEXTURE_H */

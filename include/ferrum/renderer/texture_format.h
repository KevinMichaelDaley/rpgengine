#ifndef FERRUM_RENDERER_TEXTURE_FORMAT_H
#define FERRUM_RENDERER_TEXTURE_FORMAT_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/renderer/gl_constants.h"

/** @file
 * @brief Lightweight pixel-format enum for 2D textures and its resolution to
 *        the GL (internalformat, format, type) triple.
 *
 * Colour maps (albedo, emissive, tint) use the sRGB variants so the GPU
 * linearises on sample; data maps (normal, roughness, metalness, AO, height,
 * mask) and HDR targets (the float lightmap atlas) use the linear variants.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** 2D texture pixel formats. */
typedef enum texture_format {
    TEXTURE_FORMAT_R8 = 0,     /**< linear 1-channel (roughness/metal/AO/mask). */
    TEXTURE_FORMAT_RG8,        /**< linear 2-channel. */
    TEXTURE_FORMAT_RGB8,       /**< linear 3-channel (normal/data). */
    TEXTURE_FORMAT_RGBA8,      /**< linear 4-channel. */
    TEXTURE_FORMAT_SRGB8,      /**< sRGB 3-channel colour (albedo/emissive). */
    TEXTURE_FORMAT_SRGB8_A8,   /**< sRGB 4-channel colour. */
    TEXTURE_FORMAT_RGB16F,     /**< half-float HDR 3-channel. */
    TEXTURE_FORMAT_RGBA16F,    /**< half-float HDR 4-channel. */
    TEXTURE_FORMAT_RGB32F,     /**< float 3-channel (SH-irradiance lightmap). */
    TEXTURE_FORMAT_COUNT
} texture_format_t;

/**
 * @brief Resolve a texture_format to its GL (internalformat, format, type).
 * @param fmt      Format to resolve.
 * @param internal Out: sized internal format (e.g. GL_SRGB8_ALPHA8).
 * @param format   Out: client pixel format (e.g. GL_RGBA).
 * @param type     Out: client pixel type (e.g. GL_UNSIGNED_BYTE).
 * @return true if @p fmt is a known format, false otherwise (outs untouched).
 */
static inline bool texture_format_resolve(texture_format_t fmt,
                                          uint32_t *internal, uint32_t *format,
                                          uint32_t *type)
{
    switch (fmt) {
    case TEXTURE_FORMAT_R8:
        *internal = GL_R8; *format = GL_RED; *type = GL_UNSIGNED_BYTE; return true;
    case TEXTURE_FORMAT_RG8:
        *internal = GL_RG8; *format = GL_RG; *type = GL_UNSIGNED_BYTE; return true;
    case TEXTURE_FORMAT_RGB8:
        *internal = GL_RGB8; *format = GL_RGB; *type = GL_UNSIGNED_BYTE; return true;
    case TEXTURE_FORMAT_RGBA8:
        *internal = GL_RGBA8; *format = GL_RGBA; *type = GL_UNSIGNED_BYTE; return true;
    case TEXTURE_FORMAT_SRGB8:
        *internal = GL_SRGB8; *format = GL_RGB; *type = GL_UNSIGNED_BYTE; return true;
    case TEXTURE_FORMAT_SRGB8_A8:
        *internal = GL_SRGB8_ALPHA8; *format = GL_RGBA; *type = GL_UNSIGNED_BYTE; return true;
    case TEXTURE_FORMAT_RGB16F:
        *internal = GL_RGB16F; *format = GL_RGB; *type = GL_FLOAT; return true;
    case TEXTURE_FORMAT_RGBA16F:
        *internal = GL_RGBA16F; *format = GL_RGBA; *type = GL_FLOAT; return true;
    case TEXTURE_FORMAT_RGB32F:
        *internal = GL_RGB32F; *format = GL_RGB; *type = GL_FLOAT; return true;
    case TEXTURE_FORMAT_COUNT:
    default:
        return false;
    }
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_TEXTURE_FORMAT_H */

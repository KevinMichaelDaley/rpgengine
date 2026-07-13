#include "ferrum/renderer/texture.h"

#include "ferrum/renderer/gl_constants.h"

texture_status_t texture_upload_2d(texture_t *tex, texture_format_t format,
                                   uint32_t width, uint32_t height,
                                   const void *pixels, bool gen_mips)
{
    if (tex == NULL || tex->handle == 0u || tex->glTexImage2D == NULL) {
        return TEXTURE_ERR_INVALID;
    }
    if (width == 0u || height == 0u) {
        return TEXTURE_ERR_INVALID;
    }
    uint32_t internal = 0, gl_format = 0, gl_type = 0;
    if (!texture_format_resolve(format, &internal, &gl_format, &gl_type)) {
        return TEXTURE_ERR_FORMAT;
    }

    tex->glBindTexture(tex->target, tex->handle);
    tex->glTexImage2D(tex->target, 0, (int32_t)internal, (int32_t)width,
                      (int32_t)height, 0, gl_format, gl_type, pixels);
    if (gen_mips) {
        tex->glGenerateMipmap(tex->target);
    }
    return TEXTURE_OK;
}

texture_status_t texture_set_sampler(texture_t *tex, uint32_t min_filter,
                                     uint32_t mag_filter, uint32_t wrap_s,
                                     uint32_t wrap_t)
{
    if (tex == NULL || tex->handle == 0u || tex->glTexParameteri == NULL) {
        return TEXTURE_ERR_INVALID;
    }
    tex->glBindTexture(tex->target, tex->handle);
    tex->glTexParameteri(tex->target, GL_TEXTURE_MIN_FILTER, (int32_t)min_filter);
    tex->glTexParameteri(tex->target, GL_TEXTURE_MAG_FILTER, (int32_t)mag_filter);
    tex->glTexParameteri(tex->target, GL_TEXTURE_WRAP_S, (int32_t)wrap_s);
    tex->glTexParameteri(tex->target, GL_TEXTURE_WRAP_T, (int32_t)wrap_t);
    return TEXTURE_OK;
}

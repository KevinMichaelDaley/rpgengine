#include "ferrum/renderer/texture.h"

#include "ferrum/renderer/gl_constants.h"

texture_status_t texture_bind(const texture_t *tex, uint32_t unit)
{
    if (tex == NULL || tex->handle == 0u || tex->glActiveTexture == NULL ||
        tex->glBindTexture == NULL) {
        return TEXTURE_ERR_INVALID;
    }
    tex->glActiveTexture(GL_TEXTURE0 + unit);
    tex->glBindTexture(tex->target, tex->handle);
    return TEXTURE_OK;
}

void texture_destroy(texture_t *tex)
{
    if (tex == NULL || tex->handle == 0u || tex->glDeleteTextures == NULL) {
        return;
    }
    tex->glDeleteTextures(1, &tex->handle);
    tex->handle = 0u;
}

uint32_t texture_handle(const texture_t *tex)
{
    return (tex != NULL) ? tex->handle : 0u;
}

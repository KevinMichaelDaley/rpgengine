#include "ferrum/renderer/texture.h"

#include <string.h>

#include "ferrum/renderer/gl_constants.h"

static void *texture_get_proc(const gl_loader_t *loader, const char *name)
{
    if (loader == NULL || loader->get_proc_address == NULL || name == NULL) {
        return NULL;
    }
    return loader->get_proc_address(name, loader->user_data);
}

#define TEXTURE_LOAD_PROC(field, name)                     \
    do {                                                   \
        void *raw = texture_get_proc(loader, name);        \
        if (raw == NULL) {                                 \
            return TEXTURE_ERR_MISSING_GL;                 \
        }                                                  \
        memcpy(&(field), &raw, sizeof(field));             \
    } while (0)

texture_status_t texture_create(texture_t *tex, const gl_loader_t *loader)
{
    if (tex == NULL) {
        return TEXTURE_ERR_INVALID;
    }
    memset(tex, 0, sizeof(*tex));
    if (loader == NULL || loader->get_proc_address == NULL) {
        return TEXTURE_ERR_MISSING_GL;
    }

    TEXTURE_LOAD_PROC(tex->glGenTextures, "glGenTextures");
    TEXTURE_LOAD_PROC(tex->glDeleteTextures, "glDeleteTextures");
    TEXTURE_LOAD_PROC(tex->glBindTexture, "glBindTexture");
    TEXTURE_LOAD_PROC(tex->glActiveTexture, "glActiveTexture");
    TEXTURE_LOAD_PROC(tex->glTexImage2D, "glTexImage2D");
    TEXTURE_LOAD_PROC(tex->glTexParameteri, "glTexParameteri");
    TEXTURE_LOAD_PROC(tex->glGenerateMipmap, "glGenerateMipmap");

    tex->target = GL_TEXTURE_2D;
    tex->glGenTextures(1, &tex->handle);
    if (tex->handle == 0u) {
        return TEXTURE_ERR_INVALID;
    }
    return TEXTURE_OK;
}

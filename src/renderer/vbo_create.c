#include "ferrum/renderer/vbo.h"

#include <string.h>

static void *vbo_get_proc(const gl_loader_t *loader, const char *name) {
    if (loader == NULL || loader->get_proc_address == NULL || name == NULL) {
        return NULL;
    }
    return loader->get_proc_address(name, loader->user_data);
}

#define VBO_LOAD_PROC(field, name)                         \
    do {                                                   \
        void *raw = vbo_get_proc(loader, name);            \
        if (raw == NULL) {                                 \
            return VBO_ERR_MISSING_GL;                     \
        }                                                  \
        memcpy(&(field), &raw, sizeof(field));             \
    } while (0)

vbo_status_t vbo_create(vbo_t *vbo, const gl_loader_t *loader) {
    if (vbo == NULL) {
        return VBO_ERR_INVALID;
    }
    memset(vbo, 0, sizeof(*vbo));
    if (loader == NULL || loader->get_proc_address == NULL) {
        return VBO_ERR_MISSING_GL;
    }

    VBO_LOAD_PROC(vbo->glGenBuffers, "glGenBuffers");
    VBO_LOAD_PROC(vbo->glDeleteBuffers, "glDeleteBuffers");
    VBO_LOAD_PROC(vbo->glBindBuffer, "glBindBuffer");
    VBO_LOAD_PROC(vbo->glBufferData, "glBufferData");

    vbo->glGenBuffers(1, &vbo->handle);
    if (vbo->handle == 0u) {
        return VBO_ERR_INVALID;
    }
    return VBO_OK;
}

#include "ferrum/renderer/vao.h"

#include <string.h>

static void *vao_get_proc(const gl_loader_t *loader, const char *name) {
    if (loader == NULL || loader->get_proc_address == NULL || name == NULL) {
        return NULL;
    }
    return loader->get_proc_address(name, loader->user_data);
}

#define VAO_LOAD_PROC(field, name)                         \
    do {                                                   \
        void *raw = vao_get_proc(loader, name);            \
        if (raw == NULL) {                                 \
            return VAO_ERR_MISSING_GL;                     \
        }                                                  \
        memcpy(&(field), &raw, sizeof(field));             \
    } while (0)

vao_status_t vao_create(vao_t *vao, const gl_loader_t *loader) {
    if (vao == NULL) {
        return VAO_ERR_INVALID;
    }
    memset(vao, 0, sizeof(*vao));
    if (loader == NULL || loader->get_proc_address == NULL) {
        return VAO_ERR_MISSING_GL;
    }

    VAO_LOAD_PROC(vao->glGenVertexArrays, "glGenVertexArrays");
    VAO_LOAD_PROC(vao->glDeleteVertexArrays, "glDeleteVertexArrays");
    VAO_LOAD_PROC(vao->glBindVertexArray, "glBindVertexArray");
    VAO_LOAD_PROC(vao->glEnableVertexAttribArray, "glEnableVertexAttribArray");
    VAO_LOAD_PROC(vao->glVertexAttribPointer, "glVertexAttribPointer");
    VAO_LOAD_PROC(vao->glVertexAttribIPointer, "glVertexAttribIPointer");
    VAO_LOAD_PROC(vao->glBindBuffer, "glBindBuffer");

    vao->glGenVertexArrays(1, &vao->handle);
    if (vao->handle == 0u) {
        return VAO_ERR_INVALID;
    }
    return VAO_OK;
}

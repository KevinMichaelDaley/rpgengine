/**
 * @file instance_data_ubo.c
 * @brief Instance data UBO init, upload, bind, destroy.
 */

#include "ferrum/renderer/ubo/instance_data_ubo.h"
#include "ferrum/renderer/gl_constants.h"

#include <stddef.h>
#include <string.h>

/* ── GL proc loader helper ────────────────────────────────────────── */

static void *get_proc_(const gl_loader_t *loader, const char *name) {
    if (!loader || !loader->get_proc_address || !name) { return NULL; }
    return loader->get_proc_address(name, loader->user_data);
}

#define LOAD_PROC(field, name) do { \
    void *raw = get_proc_(loader, name); \
    if (!raw) { return INSTANCE_DATA_UBO_ERR_GL; } \
    memcpy(&(field), &raw, sizeof(field)); \
} while (0)

/* ── instance_data_ubo_init ───────────────────────────────────────── */

int instance_data_ubo_init(instance_data_ubo_t *ubo,
                            const gl_loader_t *loader,
                            uint32_t binding,
                            uint32_t capacity)
{
    if (!ubo || !loader || capacity == 0) {
        return INSTANCE_DATA_UBO_ERR_INVALID;
    }

    memset(ubo, 0, sizeof(*ubo));
    ubo->binding  = binding;
    ubo->capacity = capacity;

    LOAD_PROC(ubo->glGenBuffers,     "glGenBuffers");
    LOAD_PROC(ubo->glDeleteBuffers,  "glDeleteBuffers");
    LOAD_PROC(ubo->glBindBuffer,     "glBindBuffer");
    LOAD_PROC(ubo->glBufferData,     "glBufferData");
    LOAD_PROC(ubo->glBufferSubData,  "glBufferSubData");
    LOAD_PROC(ubo->glBindBufferBase, "glBindBufferBase");

    ubo->glGenBuffers(1, &ubo->handle);
    if (ubo->handle == 0) { return INSTANCE_DATA_UBO_ERR_GL; }

    /* Allocate GPU buffer for capacity × instance_data_t. */
    size_t buf_size = (size_t)capacity * sizeof(instance_data_t);
    ubo->glBindBuffer(GL_UNIFORM_BUFFER, ubo->handle);
    ubo->glBufferData(GL_UNIFORM_BUFFER, buf_size,
                      NULL, GL_DYNAMIC_DRAW);
    ubo->glBindBuffer(GL_UNIFORM_BUFFER, 0);

    return INSTANCE_DATA_UBO_OK;
}

/* ── instance_data_ubo_upload ─────────────────────────────────────── */

int instance_data_ubo_upload(instance_data_ubo_t *ubo,
                              const instance_data_t *data,
                              uint32_t count)
{
    if (!ubo) { return INSTANCE_DATA_UBO_ERR_INVALID; }
    if (count == 0) { return INSTANCE_DATA_UBO_OK; }
    if (!data || count > ubo->capacity) {
        return (count > ubo->capacity)
            ? INSTANCE_DATA_UBO_ERR_FULL
            : INSTANCE_DATA_UBO_ERR_INVALID;
    }

    size_t upload_size = (size_t)count * sizeof(instance_data_t);
    ubo->glBindBuffer(GL_UNIFORM_BUFFER, ubo->handle);
    ubo->glBufferSubData(GL_UNIFORM_BUFFER, 0, upload_size, data);
    ubo->glBindBuffer(GL_UNIFORM_BUFFER, 0);

    return INSTANCE_DATA_UBO_OK;
}

/* ── instance_data_ubo_bind ───────────────────────────────────────── */

int instance_data_ubo_bind(const instance_data_ubo_t *ubo)
{
    if (!ubo) { return INSTANCE_DATA_UBO_ERR_INVALID; }
    ubo->glBindBufferBase(GL_UNIFORM_BUFFER, ubo->binding, ubo->handle);
    return INSTANCE_DATA_UBO_OK;
}

/* ── instance_data_ubo_destroy ────────────────────────────────────── */

void instance_data_ubo_destroy(instance_data_ubo_t *ubo)
{
    if (!ubo || ubo->handle == 0) { return; }
    ubo->glDeleteBuffers(1, &ubo->handle);
    ubo->handle = 0;
}

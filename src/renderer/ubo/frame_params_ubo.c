/**
 * @file frame_params_ubo.c
 * @brief Frame params UBO init, upload, bind, destroy.
 */

#include "ferrum/renderer/ubo/frame_params_ubo.h"
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
    if (!raw) { return FRAME_PARAMS_UBO_ERR_GL; } \
    memcpy(&(field), &raw, sizeof(field)); \
} while (0)

/* ── frame_params_ubo_init ────────────────────────────────────────── */

int frame_params_ubo_init(frame_params_ubo_t *ubo,
                           const gl_loader_t *loader,
                           uint32_t binding)
{
    if (!ubo || !loader) { return FRAME_PARAMS_UBO_ERR_INVALID; }

    memset(ubo, 0, sizeof(*ubo));
    ubo->binding = binding;

    LOAD_PROC(ubo->glGenBuffers,     "glGenBuffers");
    LOAD_PROC(ubo->glDeleteBuffers,  "glDeleteBuffers");
    LOAD_PROC(ubo->glBindBuffer,     "glBindBuffer");
    LOAD_PROC(ubo->glBufferData,     "glBufferData");
    LOAD_PROC(ubo->glBufferSubData,  "glBufferSubData");
    LOAD_PROC(ubo->glBindBufferBase, "glBindBufferBase");

    ubo->glGenBuffers(1, &ubo->handle);
    if (ubo->handle == 0) { return FRAME_PARAMS_UBO_ERR_GL; }

    /* Allocate GPU buffer sized for one frame_params_t. */
    ubo->glBindBuffer(GL_UNIFORM_BUFFER, ubo->handle);
    ubo->glBufferData(GL_UNIFORM_BUFFER, sizeof(frame_params_t),
                      NULL, GL_DYNAMIC_DRAW);
    ubo->glBindBuffer(GL_UNIFORM_BUFFER, 0);

    return FRAME_PARAMS_UBO_OK;
}

/* ── frame_params_ubo_upload ──────────────────────────────────────── */

int frame_params_ubo_upload(frame_params_ubo_t *ubo,
                             const frame_params_t *params)
{
    if (!ubo || !params) { return FRAME_PARAMS_UBO_ERR_INVALID; }

    ubo->glBindBuffer(GL_UNIFORM_BUFFER, ubo->handle);
    ubo->glBufferSubData(GL_UNIFORM_BUFFER, 0,
                         sizeof(frame_params_t), params);
    ubo->glBindBuffer(GL_UNIFORM_BUFFER, 0);

    return FRAME_PARAMS_UBO_OK;
}

/* ── frame_params_ubo_bind ────────────────────────────────────────── */

int frame_params_ubo_bind(const frame_params_ubo_t *ubo)
{
    if (!ubo) { return FRAME_PARAMS_UBO_ERR_INVALID; }
    ubo->glBindBufferBase(GL_UNIFORM_BUFFER, ubo->binding, ubo->handle);
    return FRAME_PARAMS_UBO_OK;
}

/* ── frame_params_ubo_destroy ─────────────────────────────────────── */

void frame_params_ubo_destroy(frame_params_ubo_t *ubo)
{
    if (!ubo || ubo->handle == 0) { return; }
    ubo->glDeleteBuffers(1, &ubo->handle);
    ubo->handle = 0;
}

/**
 * @file gpu_executor.c
 * @brief Render-thread GPU command executor (see gpu_executor.h).
 */
#include "ferrum/renderer/resource/gpu_executor.h"

#include <stddef.h>
#include <string.h>

#include "ferrum/renderer/gl_constants.h"

#define GE_LOAD(dst, name)                                                    \
    do {                                                                      \
        void *p_ = loader->get_proc_address((name), loader->user_data);       \
        if (p_ == NULL) return false;                                         \
        memcpy(&(dst), &p_, sizeof(p_));                                      \
    } while (0)

bool gpu_executor_init(gpu_executor_t *exec, const gl_loader_t *loader,
                       gpu_registry_t *reg)
{
    if (exec == NULL || loader == NULL || loader->get_proc_address == NULL ||
        reg == NULL)
        return false;
    memset(exec, 0, sizeof(*exec));
    exec->registry = reg;
    GE_LOAD(exec->glGenTextures, "glGenTextures");
    GE_LOAD(exec->glDeleteTextures, "glDeleteTextures");
    GE_LOAD(exec->glBindTexture, "glBindTexture");
    GE_LOAD(exec->glTexImage2D, "glTexImage2D");
    GE_LOAD(exec->glTexImage3D, "glTexImage3D");
    GE_LOAD(exec->glTexSubImage2D, "glTexSubImage2D");
    GE_LOAD(exec->glTexParameteri, "glTexParameteri");
    GE_LOAD(exec->glGenerateMipmap, "glGenerateMipmap");
    GE_LOAD(exec->glGenBuffers, "glGenBuffers");
    GE_LOAD(exec->glDeleteBuffers, "glDeleteBuffers");
    GE_LOAD(exec->glBindBuffer, "glBindBuffer");
    GE_LOAD(exec->glBufferData, "glBufferData");
    return true;
}

/* Map a GL internal format to the (external format, type) an upload expects. */
static void ge_ext_format(uint32_t internal, uint32_t *fmt, uint32_t *type)
{
    switch (internal) {
    case GL_RGB8:    *fmt = GL_RGB;  *type = GL_UNSIGNED_BYTE; break;
    case GL_R32F:    *fmt = GL_RED;  *type = GL_FLOAT;         break;
    case GL_RG32F:   *fmt = GL_RG;   *type = GL_FLOAT;         break;
    case GL_RGBA16F:
    case GL_RGBA32F: *fmt = GL_RGBA; *type = GL_FLOAT;         break;
    case GL_RGBA8:
    default:         *fmt = GL_RGBA; *type = GL_UNSIGNED_BYTE; break;
    }
}

/* Create (and optionally upload) a texture; layers>1 -> a 2D array. */
static void ge_create_texture(gpu_executor_t *exec, const gpu_cmd_t *c)
{
    gpu_resource_t *r = gpu_registry_get(exec->registry, c->target);
    if (r == NULL)
        return;
    uint32_t w = c->a, h = c->b, internal = c->c, layers = c->d;
    uint32_t fmt, type;
    ge_ext_format(internal, &fmt, &type);

    uint32_t tex = 0;
    exec->glGenTextures(1, &tex);
    uint32_t targ = (layers > 1u) ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
    exec->glBindTexture(targ, tex);
    if (layers > 1u)
        exec->glTexImage3D(targ, 0, (int32_t)internal, (int32_t)w, (int32_t)h,
                           (int32_t)layers, 0, fmt, type, c->data);
    else
        exec->glTexImage2D(targ, 0, (int32_t)internal, (int32_t)w, (int32_t)h,
                           0, fmt, type, c->data);
    exec->glTexParameteri(targ, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    exec->glTexParameteri(targ, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    exec->glTexParameteri(targ, GL_TEXTURE_WRAP_S, GL_REPEAT);
    exec->glTexParameteri(targ, GL_TEXTURE_WRAP_T, GL_REPEAT);

    r->gl_name = tex;
    r->width = w; r->height = h; r->format = internal; r->layers = layers;
    atomic_store_explicit(&r->ready, 1, memory_order_release);
}

static void ge_upload_texture(gpu_executor_t *exec, const gpu_cmd_t *c)
{
    gpu_resource_t *r = gpu_registry_get(exec->registry, c->target);
    if (r == NULL || r->gl_name == 0u || c->data == NULL)
        return;
    uint32_t fmt, type;
    ge_ext_format(r->format, &fmt, &type);
    exec->glBindTexture(GL_TEXTURE_2D, r->gl_name);
    exec->glTexSubImage2D(GL_TEXTURE_2D, 0, (int32_t)c->a, (int32_t)c->b,
                          (int32_t)c->c, (int32_t)c->d, fmt, type, c->data);
}

static void ge_destroy_texture(gpu_executor_t *exec, const gpu_cmd_t *c)
{
    gpu_resource_t *r = gpu_registry_get(exec->registry, c->target);
    if (r != NULL && r->gl_name != 0u)
        exec->glDeleteTextures(1, &r->gl_name);
    gpu_registry_free(exec->registry, c->target);
}

static void ge_create_buffer(gpu_executor_t *exec, const gpu_cmd_t *c)
{
    gpu_resource_t *r = gpu_registry_get(exec->registry, c->target);
    if (r == NULL)
        return;
    uint32_t buf = 0;
    exec->glGenBuffers(1, &buf);
    exec->glBindBuffer(GL_ARRAY_BUFFER, buf);
    exec->glBufferData(GL_ARRAY_BUFFER, (intptr_t)c->a, c->data, GL_STATIC_DRAW);
    r->gl_name = buf;
    atomic_store_explicit(&r->ready, 1, memory_order_release);
}

static void ge_destroy_buffer(gpu_executor_t *exec, const gpu_cmd_t *c)
{
    gpu_resource_t *r = gpu_registry_get(exec->registry, c->target);
    if (r != NULL && r->gl_name != 0u)
        exec->glDeleteBuffers(1, &r->gl_name);
    gpu_registry_free(exec->registry, c->target);
}

uint32_t gpu_executor_drain(gpu_executor_t *exec, gpu_cmd_queue_t *queue)
{
    if (exec == NULL || queue == NULL)
        return 0u;
    uint32_t n = 0u;
    gpu_cmd_t c;
    while (gpu_cmd_pop(queue, &c)) {
        switch (c.type) {
        case GPU_CMD_CREATE_TEXTURE:  ge_create_texture(exec, &c);  break;
        case GPU_CMD_UPLOAD_TEXTURE:  ge_upload_texture(exec, &c);  break;
        case GPU_CMD_DESTROY_TEXTURE: ge_destroy_texture(exec, &c); break;
        case GPU_CMD_CREATE_BUFFER:   ge_create_buffer(exec, &c);   break;
        case GPU_CMD_DESTROY_BUFFER:  ge_destroy_buffer(exec, &c);  break;
        default: break; /* buffer-upload / shadow ops handled elsewhere. */
        }
        ++n;
    }
    return n;
}

void gpu_executor_destroy(gpu_executor_t *exec)
{
    if (exec != NULL)
        exec->registry = NULL;
}

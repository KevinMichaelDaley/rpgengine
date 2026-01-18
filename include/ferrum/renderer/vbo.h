#ifndef FERRUM_RENDERER_VBO_H
#define FERRUM_RENDERER_VBO_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/gl_constants.h"

/** @file
 * @brief Vertex buffer object wrapper.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Status codes for VBO operations. */
typedef enum vbo_status {
    VBO_OK = 0,
    VBO_ERR_INVALID = 1,
    VBO_ERR_MISSING_GL = 2,
    VBO_ERR_ZERO_SIZE = 3
} vbo_status_t;

/** Vertex buffer object wrapper with explicit ownership. */
typedef struct vbo {
    uint32_t handle;
    void (*glGenBuffers)(int32_t count, uint32_t *buffers);
    void (*glDeleteBuffers)(int32_t count, const uint32_t *buffers);
    void (*glBindBuffer)(uint32_t target, uint32_t buffer);
    void (*glBufferData)(uint32_t target, size_t size, const void *data, uint32_t usage);
} vbo_t;

/**
 * @brief Create a VBO.
 * @param vbo Output buffer object (non-NULL).
 * @param loader GL loader table.
 * @return Status code indicating success or failure.
 */
vbo_status_t vbo_create(vbo_t *vbo, const gl_loader_t *loader);

/**
 * @brief Destroy a VBO.
 * @param vbo VBO to destroy (NULL-safe).
 */
void vbo_destroy(vbo_t *vbo);

/**
 * @brief Upload data to a VBO.
 * @param vbo VBO to update.
 * @param target GL buffer target.
 * @param data Data pointer (may be NULL for orphaning).
 * @param size Data size in bytes.
 * @param usage GL usage hint.
 * @return Status code indicating success or failure.
 */
vbo_status_t vbo_upload(vbo_t *vbo, uint32_t target, const void *data, size_t size, uint32_t usage);

/**
 * @brief Retrieve the underlying VBO handle.
 * @param vbo VBO pointer.
 * @return OpenGL buffer handle or 0 if invalid.
 */
uint32_t vbo_handle(const vbo_t *vbo);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_VBO_H */

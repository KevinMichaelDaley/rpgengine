#ifndef FERRUM_RENDERER_VAO_H
#define FERRUM_RENDERER_VAO_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/vbo.h"
#include "ferrum/renderer/vao_attribute.h"

/** @file
 * @brief Vertex array object wrapper.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Status codes for VAO operations. */
typedef enum vao_status {
    VAO_OK = 0,
    VAO_ERR_INVALID = 1,
    VAO_ERR_MISSING_GL = 2
} vao_status_t;

/** Vertex array object wrapper with explicit ownership. */
typedef struct vao {
    uint32_t handle;
    void (*glGenVertexArrays)(int32_t count, uint32_t *arrays);
    void (*glDeleteVertexArrays)(int32_t count, const uint32_t *arrays);
    void (*glBindVertexArray)(uint32_t array);
    void (*glEnableVertexAttribArray)(uint32_t index);
    void (*glVertexAttribPointer)(uint32_t index,
                                  int32_t size,
                                  uint32_t type,
                                  uint8_t normalized,
                                  size_t stride,
                                  const void *pointer);
    void (*glVertexAttribIPointer)(uint32_t index,
                                   int32_t size,
                                   uint32_t type,
                                   size_t stride,
                                   const void *pointer);
    void (*glBindBuffer)(uint32_t target, uint32_t buffer);
} vao_t;

/**
 * @brief Create a VAO.
 * @param vao Output VAO (non-NULL).
 * @param loader GL loader table.
 * @return Status code indicating success or failure.
 */
vao_status_t vao_create(vao_t *vao, const gl_loader_t *loader);

/**
 * @brief Destroy a VAO.
 * @param vao VAO to destroy (NULL-safe).
 */
void vao_destroy(vao_t *vao);

/**
 * @brief Bind vertex attributes for a VAO using a VBO.
 * @param vao VAO to configure.
 * @param vbo VBO to bind.
 * @param attributes Attribute array.
 * @param attribute_count Number of attributes in array.
 * @param stride Stride in bytes for each vertex.
 * @return Status code indicating success or failure.
 */
vao_status_t vao_bind_attributes(vao_t *vao,
                                const vbo_t *vbo,
                                const vao_attribute_t *attributes,
                                size_t attribute_count,
                                size_t stride);

/**
 * @brief Retrieve the underlying VAO handle.
 * @param vao VAO pointer.
 * @return OpenGL VAO handle or 0 if invalid.
 */
uint32_t vao_handle(const vao_t *vao);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_VAO_H */

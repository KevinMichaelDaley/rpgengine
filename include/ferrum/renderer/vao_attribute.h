#ifndef FERRUM_RENDERER_VAO_ATTRIBUTE_H
#define FERRUM_RENDERER_VAO_ATTRIBUTE_H

#include <stdint.h>

/** @file
 * @brief Vertex attribute descriptor for VAOs.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Vertex attribute description. */
typedef struct vao_attribute {
    uint32_t index;
    int components;
    uint32_t type;
    uint8_t normalized;
    uint32_t offset;
    uint8_t integer;
} vao_attribute_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_VAO_ATTRIBUTE_H */

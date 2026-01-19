#ifndef FERRUM_RENDERER_SKINNING_SHADER_H
#define FERRUM_RENDERER_SKINNING_SHADER_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/renderer/bone_palette.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/shader_program.h"

/** @file
 * @brief Skinning shader program wrapper.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Skinning attribute semantic identifiers. */
#define SKINNING_ATTRIBUTE_POSITION 0u
#define SKINNING_ATTRIBUTE_NORMAL 1u
#define SKINNING_ATTRIBUTE_TEXCOORD 2u
#define SKINNING_ATTRIBUTE_BONE_WEIGHTS 3u
#define SKINNING_ATTRIBUTE_BONE_INDICES 4u

/** Status codes for skinning shader operations. */
typedef enum skinning_shader_status {
    SKINNING_SHADER_OK = 0,
    SKINNING_SHADER_ERR_INVALID = 1,
    SKINNING_SHADER_ERR_MISSING_GL = 2,
    SKINNING_SHADER_ERR_COMPILE = 3,
    SKINNING_SHADER_ERR_LINK = 4
} skinning_shader_status_t;

/** Skinning shader wrapper. */
typedef struct skinning_shader {
    shader_program_t program;
    int32_t palette_location;
} skinning_shader_t;

/**
 * @brief Create the default skinning shader program.
 * @param shader Shader output.
 * @param loader GL loader table.
 * @param log_buffer Optional log buffer.
 * @param log_capacity Size of log buffer.
 * @return Status code.
 */
skinning_shader_status_t skinning_shader_create(skinning_shader_t *shader,
                                                const gl_loader_t *loader,
                                                char *log_buffer,
                                                size_t log_capacity);

/**
 * @brief Create a skinning shader from provided source.
 */
skinning_shader_status_t skinning_shader_create_from_source(skinning_shader_t *shader,
                                                            const gl_loader_t *loader,
                                                            const char *vertex_source,
                                                            const char *fragment_source,
                                                            char *log_buffer,
                                                            size_t log_capacity);

/**
 * @brief Bind the skinning shader and palette buffer.
 * @param shader Shader instance.
 * @param palette Bone palette buffer.
 * @return Status code.
 */
skinning_shader_status_t skinning_shader_bind(const skinning_shader_t *shader,
                                              const bone_palette_buffer_t *palette);

/**
 * @brief Destroy the skinning shader.
 * @param shader Shader instance.
 */
void skinning_shader_destroy(skinning_shader_t *shader);

/**
 * @brief Get the shader program handle.
 * @param shader Shader instance.
 * @return GL program handle or 0.
 */
uint32_t skinning_shader_program_handle(const skinning_shader_t *shader);

/**
 * @brief Get attribute location for a semantic.
 * @param attribute Attribute semantic constant.
 * @return Attribute location.
 */
uint32_t skinning_attribute_location(uint32_t attribute);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_SKINNING_SHADER_H */

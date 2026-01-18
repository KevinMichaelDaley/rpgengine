#ifndef FERRUM_RENDERER_SHADER_PROGRAM_H
#define FERRUM_RENDERER_SHADER_PROGRAM_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/renderer/gl_loader.h"

/** @file
 * @brief Shader program compilation and binding wrapper.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Status codes for shader program operations. */
typedef enum shader_program_status {
    SHADER_PROGRAM_OK = 0,
    SHADER_PROGRAM_ERR_INVALID = 1,
    SHADER_PROGRAM_ERR_MISSING_GL = 2,
    SHADER_PROGRAM_ERR_COMPILE = 3,
    SHADER_PROGRAM_ERR_LINK = 4
} shader_program_status_t;

/** Shader program wrapper with explicit ownership. */
typedef struct shader_program {
    uint32_t handle;
    uint32_t (*glCreateShader)(uint32_t type);
    void (*glShaderSource)(uint32_t shader, int32_t count, const char *const *string, const int32_t *length);
    void (*glCompileShader)(uint32_t shader);
    void (*glGetShaderiv)(uint32_t shader, uint32_t pname, int32_t *params);
    void (*glGetShaderInfoLog)(uint32_t shader, int32_t buf_size, int32_t *length, char *info_log);
    void (*glDeleteShader)(uint32_t shader);
    uint32_t (*glCreateProgram)(void);
    void (*glAttachShader)(uint32_t program, uint32_t shader);
    void (*glLinkProgram)(uint32_t program);
    void (*glGetProgramiv)(uint32_t program, uint32_t pname, int32_t *params);
    void (*glGetProgramInfoLog)(uint32_t program, int32_t buf_size, int32_t *length, char *info_log);
    void (*glUseProgram)(uint32_t program);
    void (*glDeleteProgram)(uint32_t program);
    int32_t (*glGetUniformLocation)(uint32_t program, const char *name);
    void (*glUniformMatrix4fv)(int32_t location, int32_t count, uint8_t transpose, const float *value);
    void (*glUniform3fv)(int32_t location, int32_t count, const float *value);
    void (*glUniform1i)(int32_t location, int32_t v0);
    void (*glUniform1f)(int32_t location, float v0);
} shader_program_t;

/**
 * @brief Compile and link a shader program.
 * @param program Program output (non-NULL).
 * @param loader GL loader table (non-NULL).
 * @param vertex_source Vertex shader source (non-NULL).
 * @param fragment_source Fragment shader source (non-NULL).
 * @param log_buffer Optional log buffer for compiler/linker output.
 * @param log_capacity Size of log buffer in bytes.
 * @return Status code indicating success or failure.
 */
shader_program_status_t shader_program_create(shader_program_t *program,
                                              const gl_loader_t *loader,
                                              const char *vertex_source,
                                              const char *fragment_source,
                                              char *log_buffer,
                                              size_t log_capacity);

/**
 * @brief Destroy a shader program.
 * @param program Program to destroy (NULL-safe).
 */
void shader_program_destroy(shader_program_t *program);

/**
 * @brief Bind the shader program for use.
 * @param program Program to bind.
 * @return Status code indicating success or failure.
 */
shader_program_status_t shader_program_bind(const shader_program_t *program);

/**
 * @brief Obtain the underlying program handle.
 * @param program Program pointer.
 * @return OpenGL program handle or 0 if invalid.
 */
uint32_t shader_program_handle(const shader_program_t *program);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_SHADER_PROGRAM_H */

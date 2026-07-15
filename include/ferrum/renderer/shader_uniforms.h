#ifndef FERRUM_RENDERER_SHADER_UNIFORMS_H
#define FERRUM_RENDERER_SHADER_UNIFORMS_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/renderer/shader_program.h"

/** @file
 * @brief Shader uniform setters with location caching.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of cached uniforms per program. The PBR program alone sets
 *  ~68 distinct uniforms (material + SH lightmap + forward+ cluster + cube/spot
 *  shadows); the directional CSM adds another ~16 (u_csm_vp/eye/far/split per
 *  cascade). At 64 the cache overflowed and later-resolved uniforms (u_model!)
 *  silently failed, collapsing all geometry -- keep comfortable headroom. */
#define SHADER_UNIFORM_CACHE_CAPACITY 128u

/** Uniform type identifiers tracked for mismatch checks. */
#define SHADER_UNIFORM_TYPE_MAT4 1u
#define SHADER_UNIFORM_TYPE_VEC3 2u
#define SHADER_UNIFORM_TYPE_INT 3u
#define SHADER_UNIFORM_TYPE_FLOAT 4u
#define SHADER_UNIFORM_TYPE_VEC2 5u

/** Status codes for uniform operations. */
typedef enum shader_uniform_status {
    SHADER_UNIFORM_OK = 0,
    SHADER_UNIFORM_ERR_INVALID = 1,
    SHADER_UNIFORM_ERR_MISSING_GL = 2,
    SHADER_UNIFORM_ERR_NOT_FOUND = 3,
    SHADER_UNIFORM_ERR_CACHE_FULL = 4,
    SHADER_UNIFORM_ERR_TYPE_MISMATCH = 5
} shader_uniform_status_t;

/** Uniform cache for a shader program. */
typedef struct shader_uniform_cache {
    struct {
        const char *name;
        int32_t location;
        uint8_t type;
    } entries[SHADER_UNIFORM_CACHE_CAPACITY];
    uint32_t count;
    int32_t (*glGetUniformLocation)(uint32_t program, const char *name);
    void (*glUniformMatrix4fv)(int32_t location, int32_t count, uint8_t transpose, const float *value);
    void (*glUniform3fv)(int32_t location, int32_t count, const float *value);
    void (*glUniform2fv)(int32_t location, int32_t count, const float *value);
    void (*glUniform1i)(int32_t location, int32_t v0);
    void (*glUniform1f)(int32_t location, float v0);
} shader_uniform_cache_t;

/**
 * @brief Initialize a uniform cache for a program.
 * @param cache Cache to initialize.
 * @param program Shader program (non-NULL).
 * @return Status code.
 */
shader_uniform_status_t shader_uniform_cache_init(shader_uniform_cache_t *cache,
                                                  const shader_program_t *program);

/**
 * @brief Return the number of cached uniforms.
 * @param cache Cache pointer.
 * @return Count of cached entries.
 */
uint32_t shader_uniform_cache_count(const shader_uniform_cache_t *cache);

/**
 * @brief Set a mat4 uniform.
 */
shader_uniform_status_t shader_uniform_set_mat4(shader_uniform_cache_t *cache,
                                                const shader_program_t *program,
                                                const char *name,
                                                const float *value,
                                                uint8_t transpose);

/**
 * @brief Set a vec3 uniform.
 */
shader_uniform_status_t shader_uniform_set_vec3(shader_uniform_cache_t *cache,
                                                const shader_program_t *program,
                                                const char *name,
                                                const float *value);

/**
 * @brief Set a vec2 uniform.
 */
shader_uniform_status_t shader_uniform_set_vec2(shader_uniform_cache_t *cache,
                                                const shader_program_t *program,
                                                const char *name,
                                                const float *value);

/**
 * @brief Set an int uniform.
 */
shader_uniform_status_t shader_uniform_set_int(shader_uniform_cache_t *cache,
                                               const shader_program_t *program,
                                               const char *name,
                                               int32_t value);

/**
 * @brief Set a float uniform.
 */
shader_uniform_status_t shader_uniform_set_float(shader_uniform_cache_t *cache,
                                                 const shader_program_t *program,
                                                 const char *name,
                                                 float value);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_SHADER_UNIFORMS_H */

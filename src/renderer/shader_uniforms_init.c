#include "ferrum/renderer/shader_uniforms.h"

#include <stdio.h>

#include <string.h>

static shader_uniform_status_t shader_uniform_find(shader_uniform_cache_t *cache,
                                                   const char *name,
                                                   uint8_t type,
                                                   uint32_t *out_index) {
    for (uint32_t i = 0; i < cache->count; ++i) {
        if (cache->entries[i].name != NULL && strcmp(cache->entries[i].name, name) == 0) {
            if (cache->entries[i].type != type) {
                return SHADER_UNIFORM_ERR_TYPE_MISMATCH;
            }
            if (out_index != NULL) {
                *out_index = i;
            }
            return SHADER_UNIFORM_OK;
        }
    }
    return SHADER_UNIFORM_ERR_NOT_FOUND;
}

shader_uniform_status_t shader_uniform_cache_init(shader_uniform_cache_t *cache,
                                                  const shader_program_t *program) {
    if (cache == NULL || program == NULL) {
        return SHADER_UNIFORM_ERR_INVALID;
    }
    memset(cache, 0, sizeof(*cache));
    cache->glGetUniformLocation = program->glGetUniformLocation;
    cache->glUniformMatrix4fv = program->glUniformMatrix4fv;
    cache->glUniform3fv = program->glUniform3fv;
    cache->glUniform2fv = program->glUniform2fv;
    cache->glUniform1i = program->glUniform1i;
    cache->glUniform1f = program->glUniform1f;

    if (cache->glGetUniformLocation == NULL || cache->glUniformMatrix4fv == NULL ||
        cache->glUniform3fv == NULL || cache->glUniform1i == NULL || cache->glUniform1f == NULL) {
        return SHADER_UNIFORM_ERR_MISSING_GL;
    }
    return SHADER_UNIFORM_OK;
}

uint32_t shader_uniform_cache_count(const shader_uniform_cache_t *cache) {
    if (cache == NULL) {
        return 0u;
    }
    return cache->count;
}

static shader_uniform_status_t shader_uniform_resolve(shader_uniform_cache_t *cache,
                                                      const shader_program_t *program,
                                                      const char *name,
                                                      uint8_t type,
                                                      int32_t *out_location) {
    if (cache == NULL || program == NULL || name == NULL) {
        return SHADER_UNIFORM_ERR_INVALID;
    }
    uint32_t index = 0;
    shader_uniform_status_t status = shader_uniform_find(cache, name, type, &index);
    if (status == SHADER_UNIFORM_ERR_NOT_FOUND) {
        if (cache->count >= SHADER_UNIFORM_CACHE_CAPACITY) {
            /* A full cache silently dropping a uniform renders as garbage
             * (an unset u_tint is BLACK) -- warn loudly, once per name-ish. */
            static int warned = 0;
            if (warned < 8) {
                ++warned;
                fprintf(stderr, "shader_uniforms: cache FULL (%u) -- '%s' "
                        "will never upload; raise "
                        "SHADER_UNIFORM_CACHE_CAPACITY\n",
                        (unsigned)SHADER_UNIFORM_CACHE_CAPACITY, name);
            }
            return SHADER_UNIFORM_ERR_CACHE_FULL;
        }
        index = cache->count++;
        cache->entries[index].name = name;
        cache->entries[index].type = type;
        cache->entries[index].location = -1;
        status = SHADER_UNIFORM_OK;
    }
    if (status != SHADER_UNIFORM_OK) {
        return status;
    }
    if (cache->entries[index].location < 0) {
        if (cache->glGetUniformLocation == NULL) {
            return SHADER_UNIFORM_ERR_MISSING_GL;
        }
        cache->entries[index].location = cache->glGetUniformLocation(program->handle, name);
        if (cache->entries[index].location < 0) {
            return SHADER_UNIFORM_ERR_NOT_FOUND;
        }
    }
    if (out_location != NULL) {
        *out_location = cache->entries[index].location;
    }
    return SHADER_UNIFORM_OK;
}

shader_uniform_status_t shader_uniform_set_mat4(shader_uniform_cache_t *cache,
                                                const shader_program_t *program,
                                                const char *name,
                                                const float *value,
                                                uint8_t transpose) {
    if (cache == NULL || program == NULL || name == NULL || value == NULL) {
        return SHADER_UNIFORM_ERR_INVALID;
    }
    if (cache->glUniformMatrix4fv == NULL) {
        return SHADER_UNIFORM_ERR_MISSING_GL;
    }
    int32_t location = -1;
    shader_uniform_status_t status = shader_uniform_resolve(cache, program, name,
                                                            SHADER_UNIFORM_TYPE_MAT4, &location);
    if (status != SHADER_UNIFORM_OK) {
        return status;
    }
    cache->glUniformMatrix4fv(location, 1, transpose ? 1u : 0u, value);
    return SHADER_UNIFORM_OK;
}

shader_uniform_status_t shader_uniform_set_vec3(shader_uniform_cache_t *cache,
                                                const shader_program_t *program,
                                                const char *name,
                                                const float *value) {
    if (cache == NULL || program == NULL || name == NULL || value == NULL) {
        return SHADER_UNIFORM_ERR_INVALID;
    }
    if (cache->glUniform3fv == NULL) {
        return SHADER_UNIFORM_ERR_MISSING_GL;
    }
    int32_t location = -1;
    shader_uniform_status_t status = shader_uniform_resolve(cache, program, name,
                                                            SHADER_UNIFORM_TYPE_VEC3, &location);
    if (status != SHADER_UNIFORM_OK) {
        return status;
    }
    cache->glUniform3fv(location, 1, value);
    return SHADER_UNIFORM_OK;
}

shader_uniform_status_t shader_uniform_set_vec2(shader_uniform_cache_t *cache,
                                                const shader_program_t *program,
                                                const char *name,
                                                const float *value) {
    if (cache == NULL || program == NULL || name == NULL || value == NULL) {
        return SHADER_UNIFORM_ERR_INVALID;
    }
    if (cache->glUniform2fv == NULL) {
        return SHADER_UNIFORM_ERR_MISSING_GL;
    }
    int32_t location = -1;
    shader_uniform_status_t status = shader_uniform_resolve(cache, program, name,
                                                            SHADER_UNIFORM_TYPE_VEC2, &location);
    if (status != SHADER_UNIFORM_OK) {
        return status;
    }
    cache->glUniform2fv(location, 1, value);
    return SHADER_UNIFORM_OK;
}

shader_uniform_status_t shader_uniform_set_int(shader_uniform_cache_t *cache,
                                               const shader_program_t *program,
                                               const char *name,
                                               int32_t value) {
    if (cache == NULL || program == NULL || name == NULL) {
        return SHADER_UNIFORM_ERR_INVALID;
    }
    if (cache->glUniform1i == NULL) {
        return SHADER_UNIFORM_ERR_MISSING_GL;
    }
    int32_t location = -1;
    shader_uniform_status_t status = shader_uniform_resolve(cache, program, name,
                                                            SHADER_UNIFORM_TYPE_INT, &location);
    if (status != SHADER_UNIFORM_OK) {
        return status;
    }
    cache->glUniform1i(location, value);
    return SHADER_UNIFORM_OK;
}

shader_uniform_status_t shader_uniform_set_float(shader_uniform_cache_t *cache,
                                                 const shader_program_t *program,
                                                 const char *name,
                                                 float value) {
    if (cache == NULL || program == NULL || name == NULL) {
        return SHADER_UNIFORM_ERR_INVALID;
    }
    if (cache->glUniform1f == NULL) {
        return SHADER_UNIFORM_ERR_MISSING_GL;
    }
    int32_t location = -1;
    shader_uniform_status_t status = shader_uniform_resolve(cache, program, name,
                                                            SHADER_UNIFORM_TYPE_FLOAT, &location);
    if (status != SHADER_UNIFORM_OK) {
        return status;
    }
    cache->glUniform1f(location, value);
    return SHADER_UNIFORM_OK;
}

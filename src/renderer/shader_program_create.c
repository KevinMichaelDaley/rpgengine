#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/gl_constants.h"

#include <limits.h>
#include <string.h>

static void shader_program_clear_log(char *log_buffer, size_t log_capacity) {
    if (log_buffer != NULL && log_capacity > 0u) {
        log_buffer[0] = '\0';
    }
}

static void shader_program_copy_log(char *log_buffer, size_t log_capacity, const char *message) {
    if (log_buffer == NULL || log_capacity == 0u || message == NULL) {
        return;
    }
    size_t length = strlen(message);
    if (length >= log_capacity) {
        length = log_capacity - 1u;
    }
    memcpy(log_buffer, message, length);
    log_buffer[length] = '\0';
}

static void shader_program_capture_shader_log(shader_program_t *program,
                                              uint32_t shader,
                                              char *log_buffer,
                                              size_t log_capacity) {
    if (program->glGetShaderInfoLog == NULL || log_buffer == NULL || log_capacity == 0u) {
        return;
    }
    shader_program_clear_log(log_buffer, log_capacity);
    int32_t capacity = (log_capacity > (size_t)INT32_MAX) ? INT32_MAX : (int32_t)log_capacity;
    int32_t length = 0;
    program->glGetShaderInfoLog(shader, capacity, &length, log_buffer);
    log_buffer[log_capacity - 1u] = '\0';
}

static void shader_program_capture_program_log(shader_program_t *program,
                                               uint32_t handle,
                                               char *log_buffer,
                                               size_t log_capacity) {
    if (program->glGetProgramInfoLog == NULL || log_buffer == NULL || log_capacity == 0u) {
        return;
    }
    shader_program_clear_log(log_buffer, log_capacity);
    int32_t capacity = (log_capacity > (size_t)INT32_MAX) ? INT32_MAX : (int32_t)log_capacity;
    int32_t length = 0;
    program->glGetProgramInfoLog(handle, capacity, &length, log_buffer);
    log_buffer[log_capacity - 1u] = '\0';
}

static void *shader_program_get_proc(const gl_loader_t *loader, const char *name) {
    if (loader == NULL || loader->get_proc_address == NULL || name == NULL) {
        return NULL;
    }
    return loader->get_proc_address(name, loader->user_data);
}

#define SHADER_PROGRAM_LOAD_PROC(field, name)                        \
    do {                                                             \
        void *raw = shader_program_get_proc(loader, name);           \
        if (raw == NULL) {                                           \
            shader_program_copy_log(log_buffer, log_capacity, name); \
            return 0;                                                \
        }                                                            \
        memcpy(&(field), &raw, sizeof(field));                       \
    } while (0)

static int shader_program_load_gl(shader_program_t *program,
                                  const gl_loader_t *loader,
                                  char *log_buffer,
                                  size_t log_capacity) {
    const char *missing = NULL;
    if (gl_loader_validate(loader, &missing) != GL_LOADER_OK) {
        shader_program_copy_log(log_buffer, log_capacity, missing);
        return 0;
    }

    SHADER_PROGRAM_LOAD_PROC(program->glCreateShader, "glCreateShader");
    SHADER_PROGRAM_LOAD_PROC(program->glShaderSource, "glShaderSource");
    SHADER_PROGRAM_LOAD_PROC(program->glCompileShader, "glCompileShader");
    SHADER_PROGRAM_LOAD_PROC(program->glGetShaderiv, "glGetShaderiv");
    SHADER_PROGRAM_LOAD_PROC(program->glGetShaderInfoLog, "glGetShaderInfoLog");
    SHADER_PROGRAM_LOAD_PROC(program->glDeleteShader, "glDeleteShader");
    SHADER_PROGRAM_LOAD_PROC(program->glCreateProgram, "glCreateProgram");
    SHADER_PROGRAM_LOAD_PROC(program->glAttachShader, "glAttachShader");
    SHADER_PROGRAM_LOAD_PROC(program->glLinkProgram, "glLinkProgram");
    SHADER_PROGRAM_LOAD_PROC(program->glGetProgramiv, "glGetProgramiv");
    SHADER_PROGRAM_LOAD_PROC(program->glGetProgramInfoLog, "glGetProgramInfoLog");
    SHADER_PROGRAM_LOAD_PROC(program->glUseProgram, "glUseProgram");
    SHADER_PROGRAM_LOAD_PROC(program->glDeleteProgram, "glDeleteProgram");
    SHADER_PROGRAM_LOAD_PROC(program->glGetUniformLocation, "glGetUniformLocation");
    SHADER_PROGRAM_LOAD_PROC(program->glUniformMatrix4fv, "glUniformMatrix4fv");
    SHADER_PROGRAM_LOAD_PROC(program->glUniform3fv, "glUniform3fv");
    SHADER_PROGRAM_LOAD_PROC(program->glUniform2fv, "glUniform2fv");
    SHADER_PROGRAM_LOAD_PROC(program->glUniform1i, "glUniform1i");
    SHADER_PROGRAM_LOAD_PROC(program->glUniform1f, "glUniform1f");

    return 1;
}

static shader_program_status_t shader_program_compile(shader_program_t *program,
                                                      uint32_t type,
                                                      const char *source,
                                                      uint32_t *out_shader,
                                                      char *log_buffer,
                                                      size_t log_capacity) {
    uint32_t shader = program->glCreateShader(type);
    if (shader == 0u) {
        shader_program_copy_log(log_buffer, log_capacity, "glCreateShader");
        return SHADER_PROGRAM_ERR_COMPILE;
    }
    const char *sources[] = {source};
    program->glShaderSource(shader, 1, sources, NULL);
    program->glCompileShader(shader);
    int32_t status = 0;
    program->glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        shader_program_capture_shader_log(program, shader, log_buffer, log_capacity);
        program->glDeleteShader(shader);
        return SHADER_PROGRAM_ERR_COMPILE;
    }
    *out_shader = shader;
    return SHADER_PROGRAM_OK;
}

shader_program_status_t shader_program_create(shader_program_t *program,
                                              const gl_loader_t *loader,
                                              const char *vertex_source,
                                              const char *fragment_source,
                                              char *log_buffer,
                                              size_t log_capacity) {
    if (program == NULL || vertex_source == NULL || fragment_source == NULL) {
        return SHADER_PROGRAM_ERR_INVALID;
    }
    program->handle = 0u;
    shader_program_clear_log(log_buffer, log_capacity);

    if (loader == NULL || loader->get_proc_address == NULL) {
        shader_program_copy_log(log_buffer, log_capacity, "get_proc_address");
        return SHADER_PROGRAM_ERR_MISSING_GL;
    }
    if (!shader_program_load_gl(program, loader, log_buffer, log_capacity)) {
        return SHADER_PROGRAM_ERR_MISSING_GL;
    }

    uint32_t vertex_shader = 0u;
    shader_program_status_t status = shader_program_compile(program,
                                                            (uint32_t)GL_VERTEX_SHADER,
                                                            vertex_source,
                                                            &vertex_shader,
                                                            log_buffer,
                                                            log_capacity);
    if (status != SHADER_PROGRAM_OK) {
        return status;
    }

    uint32_t fragment_shader = 0u;
    status = shader_program_compile(program,
                                    (uint32_t)GL_FRAGMENT_SHADER,
                                    fragment_source,
                                    &fragment_shader,
                                    log_buffer,
                                    log_capacity);
    if (status != SHADER_PROGRAM_OK) {
        program->glDeleteShader(vertex_shader);
        return status;
    }

    uint32_t handle = program->glCreateProgram();
    if (handle == 0u) {
        program->glDeleteShader(vertex_shader);
        program->glDeleteShader(fragment_shader);
        shader_program_copy_log(log_buffer, log_capacity, "glCreateProgram");
        return SHADER_PROGRAM_ERR_LINK;
    }

    program->glAttachShader(handle, vertex_shader);
    program->glAttachShader(handle, fragment_shader);
    program->glLinkProgram(handle);

    int32_t link_status = 0;
    program->glGetProgramiv(handle, GL_LINK_STATUS, &link_status);
    if (link_status == GL_FALSE) {
        shader_program_capture_program_log(program, handle, log_buffer, log_capacity);
        program->glDeleteShader(vertex_shader);
        program->glDeleteShader(fragment_shader);
        program->glDeleteProgram(handle);
        return SHADER_PROGRAM_ERR_LINK;
    }

    program->glDeleteShader(vertex_shader);
    program->glDeleteShader(fragment_shader);
    program->handle = handle;
    shader_program_clear_log(log_buffer, log_capacity);
    return SHADER_PROGRAM_OK;
}

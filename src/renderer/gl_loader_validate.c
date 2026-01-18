#include "ferrum/renderer/gl_loader.h"

#include <stddef.h>

static int gl_loader_has_proc(const gl_loader_t *loader, const char *name) {
    if (loader == NULL || loader->get_proc_address == NULL || name == NULL) {
        return 0;
    }
    return loader->get_proc_address(name, loader->user_data) != NULL;
}

gl_loader_status_t gl_loader_validate(const gl_loader_t *loader, const char **out_missing) {
    static const char *required[] = {
        "glCreateShader",
        "glShaderSource",
        "glCompileShader",
        "glGetShaderiv",
        "glGetShaderInfoLog",
        "glDeleteShader",
        "glCreateProgram",
        "glAttachShader",
        "glLinkProgram",
        "glGetProgramiv",
        "glGetProgramInfoLog",
        "glUseProgram",
        "glDeleteProgram",
        "glGetUniformLocation",
        "glUniformMatrix4fv",
        "glUniform3fv",
        "glUniform1i",
        "glUniform1f",
        "glGenBuffers",
        "glDeleteBuffers",
        "glBindBuffer",
        "glBufferData",
        "glGenVertexArrays",
        "glDeleteVertexArrays",
        "glBindVertexArray",
        "glEnableVertexAttribArray",
        "glVertexAttribPointer",
        "glVertexAttribIPointer"
    };
    if (out_missing != NULL) {
        *out_missing = NULL;
    }
    if (loader == NULL || loader->get_proc_address == NULL) {
        if (out_missing != NULL) {
            *out_missing = "get_proc_address";
        }
        return GL_LOADER_ERR_MISSING;
    }
    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); ++i) {
        if (!gl_loader_has_proc(loader, required[i])) {
            if (out_missing != NULL) {
                *out_missing = required[i];
            }
            return GL_LOADER_ERR_MISSING;
        }
    }
    return GL_LOADER_OK;
}

#include "ferrum/renderer/skinning_shader.h"

#include <string.h>

static const char *SKINNING_VERTEX_SOURCE =
    "#version 330 core\n"
    "layout(location = 0) in vec3 in_pos;\n"
    "layout(location = 1) in vec3 in_norm;\n"
    "layout(location = 2) in vec2 in_uv;\n"
    "layout(location = 3) in vec4 in_weights;\n"
    "layout(location = 4) in ivec4 in_indices;\n"
    "layout(std140) uniform BonePalette { mat4 bones[128]; };\n"
    "uniform mat4 u_view_proj;\n"
    "void main() {\n"
    "    mat4 skin = bones[in_indices.x] * in_weights.x +\n"
    "                bones[in_indices.y] * in_weights.y +\n"
    "                bones[in_indices.z] * in_weights.z +\n"
    "                bones[in_indices.w] * in_weights.w;\n"
    "    vec4 world = skin * vec4(in_pos, 1.0);\n"
    "    gl_Position = u_view_proj * world;\n"
    "}\n";

static const char *SKINNING_FRAGMENT_SOURCE =
    "#version 330 core\n"
    "out vec4 out_color;\n"
    "void main() {\n"
    "    out_color = vec4(1.0);\n"
    "}\n";

static skinning_shader_status_t skinning_shader_from_program_status(shader_program_status_t status) {
    if (status == SHADER_PROGRAM_ERR_MISSING_GL) {
        return SKINNING_SHADER_ERR_MISSING_GL;
    }
    if (status == SHADER_PROGRAM_ERR_COMPILE) {
        return SKINNING_SHADER_ERR_COMPILE;
    }
    if (status == SHADER_PROGRAM_ERR_LINK) {
        return SKINNING_SHADER_ERR_LINK;
    }
    if (status != SHADER_PROGRAM_OK) {
        return SKINNING_SHADER_ERR_INVALID;
    }
    return SKINNING_SHADER_OK;
}

skinning_shader_status_t skinning_shader_create_from_source(skinning_shader_t *shader,
                                                            const gl_loader_t *loader,
                                                            const char *vertex_source,
                                                            const char *fragment_source,
                                                            char *log_buffer,
                                                            size_t log_capacity) {
    if (shader == NULL || vertex_source == NULL || fragment_source == NULL) {
        return SKINNING_SHADER_ERR_INVALID;
    }
    memset(shader, 0, sizeof(*shader));
    shader_program_status_t status = shader_program_create(&shader->program,
                                                           loader,
                                                           vertex_source,
                                                           fragment_source,
                                                           log_buffer,
                                                           log_capacity);
    skinning_shader_status_t mapped = skinning_shader_from_program_status(status);
    if (mapped != SKINNING_SHADER_OK) {
        return mapped;
    }
    if (shader->program.glGetUniformLocation != NULL) {
        shader->palette_location = shader->program.glGetUniformLocation(shader->program.handle, "u_view_proj");
    } else {
        shader->palette_location = -1;
    }
    return SKINNING_SHADER_OK;
}

skinning_shader_status_t skinning_shader_create(skinning_shader_t *shader,
                                                const gl_loader_t *loader,
                                                char *log_buffer,
                                                size_t log_capacity) {
    return skinning_shader_create_from_source(shader,
                                              loader,
                                              SKINNING_VERTEX_SOURCE,
                                              SKINNING_FRAGMENT_SOURCE,
                                              log_buffer,
                                              log_capacity);
}

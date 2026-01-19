#include "ferrum/renderer/skinning_shader.h"

skinning_shader_status_t skinning_shader_bind(const skinning_shader_t *shader,
                                              const bone_palette_buffer_t *palette) {
    if (shader == NULL || palette == NULL) {
        return SKINNING_SHADER_ERR_INVALID;
    }
    if (shader_program_bind(&shader->program) != SHADER_PROGRAM_OK) {
        return SKINNING_SHADER_ERR_INVALID;
    }
    if (bone_palette_buffer_bind(palette) != BONE_PALETTE_OK) {
        return SKINNING_SHADER_ERR_INVALID;
    }
    return SKINNING_SHADER_OK;
}

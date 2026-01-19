#include "ferrum/renderer/skinning_shader.h"

uint32_t skinning_shader_program_handle(const skinning_shader_t *shader) {
    if (shader == NULL) {
        return 0u;
    }
    return shader_program_handle(&shader->program);
}

uint32_t skinning_attribute_location(uint32_t attribute) {
    switch (attribute) {
        case SKINNING_ATTRIBUTE_POSITION:
        case SKINNING_ATTRIBUTE_NORMAL:
        case SKINNING_ATTRIBUTE_TEXCOORD:
        case SKINNING_ATTRIBUTE_BONE_WEIGHTS:
        case SKINNING_ATTRIBUTE_BONE_INDICES:
            return attribute;
        default:
            return 0u;
    }
}

#include "ferrum/renderer/skinning_shader.h"

void skinning_shader_destroy(skinning_shader_t *shader) {
    if (shader == NULL) {
        return;
    }
    shader_program_destroy(&shader->program);
    shader->palette_location = 0;
}

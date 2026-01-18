#include "ferrum/renderer/bone_palette.h"
#include "ferrum/renderer/gl_constants.h"

static uint32_t bone_palette_target(const bone_palette_buffer_t *palette) {
    if (palette->type == BONE_PALETTE_BUFFER_SSBO) {
        return GL_SHADER_STORAGE_BUFFER;
    }
    if (palette->type == BONE_PALETTE_BUFFER_TBO) {
        return GL_TEXTURE_BUFFER;
    }
    return GL_UNIFORM_BUFFER;
}

bone_palette_status_t bone_palette_buffer_bind(const bone_palette_buffer_t *palette) {
    if (palette == NULL || palette->handle == 0u) {
        return BONE_PALETTE_ERR_INVALID;
    }
    if (palette->type == BONE_PALETTE_BUFFER_TBO) {
        if (palette->glActiveTexture == NULL || palette->glBindTexture == NULL) {
            return BONE_PALETTE_ERR_MISSING_GL;
        }
        palette->glActiveTexture(GL_TEXTURE0 + palette->binding_point);
        palette->glBindTexture(GL_TEXTURE_BUFFER, palette->texture_handle);
        return BONE_PALETTE_OK;
    }
    if (palette->glBindBufferBase == NULL) {
        return BONE_PALETTE_ERR_MISSING_GL;
    }
    palette->glBindBufferBase(bone_palette_target(palette), palette->binding_point, palette->handle);
    return BONE_PALETTE_OK;
}

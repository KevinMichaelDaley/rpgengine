#include "ferrum/renderer/bone_palette.h"
#include "ferrum/renderer/gl_constants.h"

static uint32_t bone_palette_target(const bone_palette_buffer_t *palette) {
    return (palette->type == BONE_PALETTE_BUFFER_SSBO) ? GL_SHADER_STORAGE_BUFFER : GL_UNIFORM_BUFFER;
}

bone_palette_status_t bone_palette_buffer_update(bone_palette_buffer_t *palette,
                                                 const void *data,
                                                 size_t size) {
    if (palette == NULL || palette->handle == 0u || data == NULL || size == 0u) {
        return BONE_PALETTE_ERR_INVALID;
    }
    size_t max_size = (size_t)palette->max_bones * BONE_PALETTE_MATRIX_FLOATS * sizeof(float);
    if (size > max_size) {
        return BONE_PALETTE_ERR_TOO_LARGE;
    }
    if (palette->glBindBuffer == NULL || palette->glBufferSubData == NULL) {
        return BONE_PALETTE_ERR_MISSING_GL;
    }
    uint32_t target = bone_palette_target(palette);
    palette->glBindBuffer(target, palette->handle);
    palette->glBufferSubData(target, 0, size, data);
    return BONE_PALETTE_OK;
}

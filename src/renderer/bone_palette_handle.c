#include "ferrum/renderer/bone_palette.h"

uint32_t bone_palette_buffer_type(const bone_palette_buffer_t *palette) {
    if (palette == NULL) {
        return BONE_PALETTE_BUFFER_UBO;
    }
    return palette->type;
}

uint32_t bone_palette_buffer_handle(const bone_palette_buffer_t *palette) {
    if (palette == NULL) {
        return 0u;
    }
    return palette->handle;
}

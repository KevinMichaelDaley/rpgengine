#include "ferrum/renderer/bone_palette.h"

void bone_palette_buffer_destroy(bone_palette_buffer_t *palette) {
    if (palette == NULL) {
        return;
    }
    if (palette->texture_handle != 0u && palette->glDeleteTextures != NULL) {
        palette->glDeleteTextures(1, &palette->texture_handle);
        palette->texture_handle = 0u;
    }
    if (palette->handle != 0u && palette->glDeleteBuffers != NULL) {
        palette->glDeleteBuffers(1, &palette->handle);
    }
    palette->handle = 0u;
}

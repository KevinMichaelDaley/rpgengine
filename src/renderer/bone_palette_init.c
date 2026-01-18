#include "ferrum/renderer/bone_palette.h"
#include "ferrum/renderer/gl_constants.h"

#include <string.h>

static void *bone_palette_get_proc(const gl_loader_t *loader, const char *name) {
    if (loader == NULL || loader->get_proc_address == NULL || name == NULL) {
        return NULL;
    }
    return loader->get_proc_address(name, loader->user_data);
}

#define PALETTE_LOAD_PROC(field, name)                      \
    do {                                                     \
        void *raw = bone_palette_get_proc(loader, name);     \
        if (raw == NULL) {                                   \
            return BONE_PALETTE_ERR_MISSING_GL;              \
        }                                                    \
        memcpy(&(field), &raw, sizeof(field));               \
    } while (0)

bone_palette_status_t bone_palette_buffer_init(bone_palette_buffer_t *palette,
                                               const gl_loader_t *loader,
                                               uint32_t max_bones,
                                               uint32_t binding_point,
                                               int supports_ssbo,
                                               int supports_tbo) {
    if (palette == NULL || loader == NULL || loader->get_proc_address == NULL || max_bones == 0u) {
        return BONE_PALETTE_ERR_INVALID;
    }
    memset(palette, 0, sizeof(*palette));
    palette->binding_point = binding_point;
    palette->max_bones = max_bones;
    palette->type = supports_ssbo ? BONE_PALETTE_BUFFER_SSBO : (supports_tbo ? BONE_PALETTE_BUFFER_TBO : BONE_PALETTE_BUFFER_UBO);

    PALETTE_LOAD_PROC(palette->glGenBuffers, "glGenBuffers");
    PALETTE_LOAD_PROC(palette->glDeleteBuffers, "glDeleteBuffers");
    PALETTE_LOAD_PROC(palette->glBindBuffer, "glBindBuffer");
    PALETTE_LOAD_PROC(palette->glBufferData, "glBufferData");
    PALETTE_LOAD_PROC(palette->glBufferSubData, "glBufferSubData");
    PALETTE_LOAD_PROC(palette->glBindBufferBase, "glBindBufferBase");
    PALETTE_LOAD_PROC(palette->glGenTextures, "glGenTextures");
    PALETTE_LOAD_PROC(palette->glDeleteTextures, "glDeleteTextures");
    PALETTE_LOAD_PROC(palette->glBindTexture, "glBindTexture");
    PALETTE_LOAD_PROC(palette->glTexBuffer, "glTexBuffer");
    PALETTE_LOAD_PROC(palette->glActiveTexture, "glActiveTexture");

    palette->glGenBuffers(1, &palette->handle);
    if (palette->handle == 0u) {
        return BONE_PALETTE_ERR_INVALID;
    }

    size_t size_bytes = (size_t)max_bones * BONE_PALETTE_MATRIX_FLOATS * sizeof(float);
    uint32_t target = GL_UNIFORM_BUFFER;
    if (palette->type == BONE_PALETTE_BUFFER_SSBO) {
        target = GL_SHADER_STORAGE_BUFFER;
    } else if (palette->type == BONE_PALETTE_BUFFER_TBO) {
        target = GL_TEXTURE_BUFFER;
    }
    palette->glBindBuffer(target, palette->handle);
    palette->glBufferData(target, size_bytes, NULL, GL_DYNAMIC_DRAW);

    if (palette->type == BONE_PALETTE_BUFFER_TBO) {
        palette->glGenTextures(1, &palette->texture_handle);
        palette->glBindTexture(GL_TEXTURE_BUFFER, palette->texture_handle);
        palette->glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, palette->handle);
    }

    return BONE_PALETTE_OK;
}

#ifndef FERRUM_RENDERER_BONE_PALETTE_H
#define FERRUM_RENDERER_BONE_PALETTE_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/renderer/gl_loader.h"

/** @file
 * @brief Bone palette buffer wrapper for skinning.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Bone matrix size in floats (4x4). */
#define BONE_PALETTE_MATRIX_FLOATS 16u

/** Bone palette buffer types. */
#define BONE_PALETTE_BUFFER_SSBO 0u
#define BONE_PALETTE_BUFFER_UBO 1u
#define BONE_PALETTE_BUFFER_TBO 2u

/** Status codes for bone palette operations. */
typedef enum bone_palette_status {
    BONE_PALETTE_OK = 0,
    BONE_PALETTE_ERR_INVALID = 1,
    BONE_PALETTE_ERR_MISSING_GL = 2,
    BONE_PALETTE_ERR_TOO_LARGE = 3
} bone_palette_status_t;

/** Bone palette buffer wrapper. */
typedef struct bone_palette_buffer {
    uint32_t handle;
    uint32_t texture_handle;
    uint8_t type;
    uint32_t binding_point;
    uint32_t max_bones;
    void (*glGenBuffers)(int32_t count, uint32_t *buffers);
    void (*glDeleteBuffers)(int32_t count, const uint32_t *buffers);
    void (*glBindBuffer)(uint32_t target, uint32_t buffer);
    void (*glBufferData)(uint32_t target, size_t size, const void *data, uint32_t usage);
    void (*glBufferSubData)(uint32_t target, size_t offset, size_t size, const void *data);
    void (*glBindBufferBase)(uint32_t target, uint32_t index, uint32_t buffer);
    void (*glGenTextures)(int32_t count, uint32_t *textures);
    void (*glDeleteTextures)(int32_t count, const uint32_t *textures);
    void (*glBindTexture)(uint32_t target, uint32_t texture);
    void (*glTexBuffer)(uint32_t target, uint32_t internalformat, uint32_t buffer);
    void (*glActiveTexture)(uint32_t texture);
} bone_palette_buffer_t;

/**
 * @brief Initialize a bone palette buffer.
 * @param palette Palette buffer to initialize.
 * @param loader GL loader table.
 * @param max_bones Maximum bones supported.
 * @param binding_point Binding point index.
 * @param supports_ssbo Non-zero if SSBO is supported.
 * @param supports_tbo Non-zero if TBO is supported.
 * @return Status code.
 */
bone_palette_status_t bone_palette_buffer_init(bone_palette_buffer_t *palette,
                                               const gl_loader_t *loader,
                                               uint32_t max_bones,
                                               uint32_t binding_point,
                                               int supports_ssbo,
                                               int supports_tbo);

/**
 * @brief Destroy palette buffer.
 * @param palette Palette buffer to destroy.
 */
void bone_palette_buffer_destroy(bone_palette_buffer_t *palette);

/**
 * @brief Update palette buffer contents.
 * @param palette Palette buffer.
 * @param data Bone matrix data.
 * @param size Size in bytes.
 * @return Status code.
 */
bone_palette_status_t bone_palette_buffer_update(bone_palette_buffer_t *palette,
                                                 const void *data,
                                                 size_t size);

/**
 * @brief Bind palette buffer to its binding point.
 * @param palette Palette buffer.
 * @return Status code.
 */
bone_palette_status_t bone_palette_buffer_bind(const bone_palette_buffer_t *palette);

/**
 * @brief Get palette buffer type.
 * @param palette Palette buffer.
 * @return Buffer type.
 */
uint32_t bone_palette_buffer_type(const bone_palette_buffer_t *palette);

/**
 * @brief Get palette buffer handle.
 * @param palette Palette buffer.
 * @return GL buffer handle or 0.
 */
uint32_t bone_palette_buffer_handle(const bone_palette_buffer_t *palette);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_BONE_PALETTE_H */

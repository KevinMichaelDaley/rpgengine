#ifndef FERRUM_RENDERER_UBO_FRAME_PARAMS_UBO_H
#define FERRUM_RENDERER_UBO_FRAME_PARAMS_UBO_H

/**
 * @file frame_params_ubo.h
 * @brief Per-frame global uniform block (UBO binding 1).
 *
 * Contains view/projection matrices, camera position, and time.
 * Uploaded once per frame before all passes.
 *
 * Layout follows std140 rules (all mat4 are column-major, 16-byte aligned).
 *
 * @note Ownership: the UBO owns its GL buffer.
 * @note Nullability: all pointer parameters must be non-NULL.
 * @note Error semantics: functions return status codes.
 */

#include <stdint.h>

#include "ferrum/renderer/gl_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ────────────────────────────────────────────────────── */

/** Default UBO binding point for frame params. */
#define FRAME_PARAMS_BINDING 1u

/* ── Status codes ─────────────────────────────────────────────────── */

enum {
    FRAME_PARAMS_UBO_OK          = 0,
    FRAME_PARAMS_UBO_ERR_INVALID = 1,
    FRAME_PARAMS_UBO_ERR_GL      = 2
};

/* ── Data layout (std140) ─────────────────────────────────────────── */

/**
 * @brief Per-frame global uniform data.
 *
 * Must be std140-compatible (size must be multiple of 16).
 */
typedef struct frame_params {
    float view[16];          /**< View matrix (column-major). */
    float proj[16];          /**< Projection matrix (column-major). */
    float view_proj[16];     /**< View × Projection (column-major). */
    float camera_pos[4];     /**< Camera world position (w unused, pad). */
    float time;              /**< Elapsed time in seconds. */
    float _pad[3];           /**< Padding to 16-byte alignment. */
} frame_params_t;

/* ── UBO wrapper ──────────────────────────────────────────────────── */

/**
 * @brief GL buffer wrapper for frame params UBO.
 */
typedef struct frame_params_ubo {
    uint32_t handle;          /**< GL buffer object name. */
    uint32_t binding;         /**< UBO binding point. */

    /* GL function pointers (resolved at init). */
    void (*glGenBuffers)(int32_t, uint32_t *);
    void (*glDeleteBuffers)(int32_t, const uint32_t *);
    void (*glBindBuffer)(uint32_t, uint32_t);
    void (*glBufferData)(uint32_t, size_t, const void *, uint32_t);
    void (*glBufferSubData)(uint32_t, size_t, size_t, const void *);
    void (*glBindBufferBase)(uint32_t, uint32_t, uint32_t);
} frame_params_ubo_t;

/* ── API ──────────────────────────────────────────────────────────── */

/**
 * @brief Initialize the frame params UBO.
 *
 * Creates a GL buffer sized for one frame_params_t and resolves
 * GL function pointers via the loader.
 *
 * @param ubo      Output UBO (non-NULL).
 * @param loader   GL function loader (non-NULL).
 * @param binding  UBO binding point (typically FRAME_PARAMS_BINDING).
 * @return Status code.
 */
int frame_params_ubo_init(frame_params_ubo_t *ubo,
                           const gl_loader_t *loader,
                           uint32_t binding);

/**
 * @brief Upload frame params to the UBO.
 *
 * @param ubo     UBO (non-NULL, must be initialized).
 * @param params  Frame params data (non-NULL).
 * @return Status code.
 */
int frame_params_ubo_upload(frame_params_ubo_t *ubo,
                             const frame_params_t *params);

/**
 * @brief Bind the UBO to its binding point.
 *
 * @param ubo  UBO (non-NULL).
 * @return Status code.
 */
int frame_params_ubo_bind(const frame_params_ubo_t *ubo);

/**
 * @brief Destroy the UBO, releasing the GL buffer.
 *
 * Safe to call with NULL.
 *
 * @param ubo  UBO to destroy (NULL-safe).
 */
void frame_params_ubo_destroy(frame_params_ubo_t *ubo);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_UBO_FRAME_PARAMS_UBO_H */

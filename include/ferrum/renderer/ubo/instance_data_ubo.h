#ifndef FERRUM_RENDERER_UBO_INSTANCE_DATA_UBO_H
#define FERRUM_RENDERER_UBO_INSTANCE_DATA_UBO_H

/**
 * @file instance_data_ubo.h
 * @brief Per-instance model matrix UBO (UBO binding 2).
 *
 * Holds an array of per-instance data (model matrix, inverse-transpose
 * for normals, entity ID for picking).  Capacity is configurable at
 * init time — no compile-time limit.
 *
 * Layout follows std140 rules.  Each instance_data_t is 144 bytes
 * (2 × mat4 + uint + 3 × uint pad = 128 + 16 = 144), padded to
 * 16-byte alignment for std140 array stride.
 *
 * @note Ownership: the UBO owns its GL buffer.
 * @note Nullability: all pointer parameters must be non-NULL.
 */

#include <stdint.h>

#include "ferrum/renderer/gl_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ────────────────────────────────────────────────────── */

/** Default UBO binding point for instance data. */
#define INSTANCE_DATA_BINDING 2u

/* ── Status codes ─────────────────────────────────────────────────── */

enum {
    INSTANCE_DATA_UBO_OK          = 0,
    INSTANCE_DATA_UBO_ERR_INVALID = 1,
    INSTANCE_DATA_UBO_ERR_GL      = 2,
    INSTANCE_DATA_UBO_ERR_FULL    = 3
};

/* ── Data layout (std140) ─────────────────────────────────────────── */

/**
 * @brief Per-instance uniform data.
 *
 * Size must be multiple of 16 for std140 array stride.
 * 2 × mat4 (128) + uint32 (4) + 3 × uint32 pad (12) = 144 bytes.
 */
typedef struct instance_data {
    float    model[16];               /**< Model matrix (column-major). */
    float    model_inv_transpose[16]; /**< Inverse-transpose for normals. */
    uint32_t entity_id;               /**< Entity ID for picking/debug. */
    uint32_t _pad[3];                 /**< Padding to 16-byte alignment. */
} instance_data_t;

/* ── UBO wrapper ──────────────────────────────────────────────────── */

/**
 * @brief GL buffer wrapper for instance data UBO.
 */
typedef struct instance_data_ubo {
    uint32_t handle;          /**< GL buffer object name. */
    uint32_t binding;         /**< UBO binding point. */
    uint32_t capacity;        /**< Maximum instances (set at init). */

    /* GL function pointers (resolved at init). */
    void (*glGenBuffers)(int32_t, uint32_t *);
    void (*glDeleteBuffers)(int32_t, const uint32_t *);
    void (*glBindBuffer)(uint32_t, uint32_t);
    void (*glBufferData)(uint32_t, size_t, const void *, uint32_t);
    void (*glBufferSubData)(uint32_t, size_t, size_t, const void *);
    void (*glBindBufferBase)(uint32_t, uint32_t, uint32_t);
} instance_data_ubo_t;

/* ── API ──────────────────────────────────────────────────────────── */

/**
 * @brief Initialize the instance data UBO.
 *
 * Creates a GL buffer sized for `capacity` instances.
 *
 * @param ubo       Output UBO (non-NULL).
 * @param loader    GL function loader (non-NULL).
 * @param binding   UBO binding point (typically INSTANCE_DATA_BINDING).
 * @param capacity  Maximum instances (> 0).
 * @return Status code.
 */
int instance_data_ubo_init(instance_data_ubo_t *ubo,
                            const gl_loader_t *loader,
                            uint32_t binding,
                            uint32_t capacity);

/**
 * @brief Upload instance data to the UBO.
 *
 * Uploads `count` instances via glBufferSubData.  count=0 is a no-op.
 *
 * @param ubo    UBO (non-NULL, must be initialized).
 * @param data   Instance data array (non-NULL if count > 0).
 * @param count  Number of instances to upload (must be ≤ capacity).
 * @return Status code.
 */
int instance_data_ubo_upload(instance_data_ubo_t *ubo,
                              const instance_data_t *data,
                              uint32_t count);

/**
 * @brief Bind the UBO to its binding point.
 *
 * @param ubo  UBO (non-NULL).
 * @return Status code.
 */
int instance_data_ubo_bind(const instance_data_ubo_t *ubo);

/**
 * @brief Destroy the UBO, releasing the GL buffer.
 *
 * Safe to call with NULL.
 *
 * @param ubo  UBO to destroy (NULL-safe).
 */
void instance_data_ubo_destroy(instance_data_ubo_t *ubo);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_UBO_INSTANCE_DATA_UBO_H */

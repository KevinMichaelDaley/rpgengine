/**
 * @file lm_denoise.h
 * @brief Denoise a baked HDR lightmap atlas image with Intel Open Image Denoise.
 *
 * A Monte-Carlo lightmap bake is noisy at low sample counts. This wraps OIDN's
 * ray-tracing ("RT") filter to clean one HDR (linear, unbounded) atlas image in
 * place, so a low-spp bake can approach a much higher-spp reference. It operates
 * on a single float RGB image (e.g. one SH coefficient band, or a reconstructed
 * irradiance atlas); the caller decides which band(s) to denoise.
 *
 * Two build variants share this header:
 *   - real (built with OIDN=1): links libOpenImageDenoise; denoising runs.
 *   - stub (default): @ref lm_denoise_image is a well-defined no-op that returns
 *     @ref LM_DENOISE_OK and leaves the buffer untouched, and
 *     @ref lm_denoise_available returns false. This keeps the headless build and
 *     CI compiling without the heavy OIDN toolchain (ISPC/TBB); production bakes
 *     link the real library on the GPU box.
 *
 * Ownership: the caller owns all buffers; @c rgb is modified in place, the aux
 * buffers are read-only. Nullability: @c rgb non-NULL; @c albedo / @c normal
 * optional (NULL to omit). Errors: status enum. Side effects: rewrites @c rgb.
 * Offline / not perf-critical (uses malloc); never call per frame.
 */
#ifndef FERRUM_LIGHTMAP_LM_DENOISE_H
#define FERRUM_LIGHTMAP_LM_DENOISE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Result of a denoise request. */
typedef enum lm_denoise_status {
    LM_DENOISE_OK = 0,        /**< Denoised (or stub no-op) successfully. */
    LM_DENOISE_INVALID_ARG,   /**< NULL image/rgb or a zero dimension. */
    LM_DENOISE_DEVICE_ERROR,  /**< OIDN device creation/commit failed. */
    LM_DENOISE_FILTER_ERROR   /**< OIDN filter setup/execution failed. */
} lm_denoise_status_t;

/**
 * One HDR image to denoise, plus optional guide buffers. All buffers are
 * @c width*height*3 floats, row-major, 3 channels (RGB). @c rgb is the noisy
 * beauty (denoised in place). @c albedo / @c normal are optional guides that
 * sharpen edge retention (NULL to omit); when both are given OIDN uses them.
 */
typedef struct lm_denoise_image {
    float       *rgb;    /**< In/out beauty, width*height*3 floats. Non-NULL. */
    const float *albedo; /**< Optional albedo guide, or NULL. */
    const float *normal; /**< Optional world/geo normal guide, or NULL. */
    uint32_t     width;
    uint32_t     height;
} lm_denoise_image_t;

/**
 * @brief Denoise @p img->rgb in place with OIDN's RT filter in HDR mode,
 *        using @p img->albedo and @p img->normal as guides when both are set.
 * @return LM_DENOISE_OK on success (also the stub's no-op result);
 *         LM_DENOISE_INVALID_ARG if @p img, @p img->rgb are NULL or a dimension
 *         is 0; LM_DENOISE_DEVICE_ERROR / LM_DENOISE_FILTER_ERROR on OIDN
 *         failure (real build only).
 */
lm_denoise_status_t lm_denoise_image(lm_denoise_image_t *img);

/**
 * @brief Whether a real OIDN backend is compiled in (true) or this is the stub
 *        no-op build (false). Callers can skip guide-buffer preparation, or log,
 *        based on this.
 */
bool lm_denoise_available(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_LIGHTMAP_LM_DENOISE_H */

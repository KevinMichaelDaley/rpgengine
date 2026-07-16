/**
 * @file lm_denoise_stub.c
 * @brief No-op OIDN stub: default build when the library is not linked.
 *
 * Validates arguments identically to the real backend, then returns without
 * touching the image, so the bake pipeline is a well-defined pass-through when
 * OIDN is absent. @ref lm_denoise_available reports false so callers/tests can
 * tell the two apart. See @ref lm_denoise.h.
 */
#include "ferrum/lightmap/lm_denoise.h"

#include <stddef.h>

lm_denoise_status_t lm_denoise_image(lm_denoise_image_t *img)
{
    if (img == NULL || img->rgb == NULL || img->width == 0u ||
        img->height == 0u) {
        return LM_DENOISE_INVALID_ARG;
    }
    /* No backend: leave the buffer untouched. */
    return LM_DENOISE_OK;
}

bool lm_denoise_available(void)
{
    return false;
}

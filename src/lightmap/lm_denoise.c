/**
 * @file lm_denoise.c
 * @brief Real Open Image Denoise backend for HDR lightmap atlases.
 *
 * Wraps OIDN's "RT" filter in HDR mode over one float-RGB image, denoising in
 * place. When both albedo and normal guides are supplied they are attached to
 * sharpen edge retention. Compiled only when the build links libOpenImageDenoise
 * (Makefile OIDN=1, which also defines FR_OIDN_ENABLE); otherwise
 * lm_denoise_stub.c provides a no-op. See @ref lm_denoise.h.
 *
 * A fresh device+filter is created per call: this is an offline bake-time pass
 * (one call per atlas coefficient), so setup cost is irrelevant and per-call
 * isolation keeps error handling simple.
 */
#include "ferrum/lightmap/lm_denoise.h"

#include <stddef.h>

#include <OpenImageDenoise/oidn.h>

/* Attach one width*height*3 float image as a densely packed RGB filter buffer. */
static void set_image(OIDNFilter filter, const char *name, const float *ptr,
                      uint32_t width, uint32_t height)
{
    /* Cast away const: OIDN takes void*; guide/beauty buffers are our own and
       the RT filter only reads guides / reads+writes the beauty we pass. */
    oidnSetSharedFilterImage(filter, name, (void *)(size_t)ptr,
                             OIDN_FORMAT_FLOAT3, width, height,
                             /*byteOffset*/ 0,
                             /*pixelByteStride*/ 0, /*rowByteStride*/ 0);
}

lm_denoise_status_t lm_denoise_image(lm_denoise_image_t *img)
{
    if (img == NULL || img->rgb == NULL || img->width == 0u ||
        img->height == 0u) {
        return LM_DENOISE_INVALID_ARG;
    }

    OIDNDevice device = oidnNewDevice(OIDN_DEVICE_TYPE_DEFAULT);
    if (device == NULL)
        return LM_DENOISE_DEVICE_ERROR;
    oidnCommitDevice(device);
    const char *msg = NULL;
    if (oidnGetDeviceError(device, &msg) != OIDN_ERROR_NONE) {
        oidnReleaseDevice(device);
        return LM_DENOISE_DEVICE_ERROR;
    }

    OIDNFilter filter = oidnNewFilter(device, "RT");
    if (filter == NULL) {
        oidnReleaseDevice(device);
        return LM_DENOISE_FILTER_ERROR;
    }

    /* In-place beauty (color == output is supported by OIDN). */
    set_image(filter, "color", img->rgb, img->width, img->height);
    set_image(filter, "output", img->rgb, img->width, img->height);

    /* Optional guides sharpen edges; only meaningful when both are present. */
    if (img->albedo != NULL && img->normal != NULL) {
        set_image(filter, "albedo", img->albedo, img->width, img->height);
        set_image(filter, "normal", img->normal, img->width, img->height);
    }

    /* Lightmap irradiance is HDR (unbounded, linear). */
    oidnSetFilterBool(filter, "hdr", true);
    oidnCommitFilter(filter);

    oidnExecuteFilter(filter);

    lm_denoise_status_t status = LM_DENOISE_OK;
    if (oidnGetDeviceError(device, &msg) != OIDN_ERROR_NONE)
        status = LM_DENOISE_FILTER_ERROR;

    oidnReleaseFilter(filter);
    oidnReleaseDevice(device);
    return status;
}

bool lm_denoise_available(void)
{
    return true;
}

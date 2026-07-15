/**
 * @file egl_headless.h
 * @brief Create a headless (surfaceless) OpenGL core context via EGL, for GPU
 *        compute on machines with no X server / display (e.g. the chimera GPU
 *        box). No window, no SDL -- just a current context you can drive with
 *        the project's gl_loader_t (proc addresses via eglGetProcAddress).
 *
 * Ownership: a single process-global context; @ref egl_headless_shutdown
 * releases it. Error semantics: every call returns false / logs to stderr on
 * failure. Side effects: makes the created context current on the calling
 * thread.
 */
#ifndef FERRUM_RENDERER_EGL_HEADLESS_H
#define FERRUM_RENDERER_EGL_HEADLESS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create + make current a surfaceless OpenGL core context of at least
 *        @p major.@p minor. Returns false if EGL cannot be initialised, no
 *        config/context is available, or make-current fails.
 */
bool egl_headless_init(int major, int minor);

/** @brief Release the context and terminate EGL. NULL-safe / idempotent. */
void egl_headless_shutdown(void);

/**
 * @brief gl_loader_t-compatible proc-address getter (eglGetProcAddress).
 * @param name  GL entry-point name.
 * @param user  Ignored.
 */
void *egl_headless_getproc(const char *name, void *user);

/** @brief Single-argument proc getter for glad's GLADloadproc. */
void *egl_headless_getproc_glad(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_EGL_HEADLESS_H */

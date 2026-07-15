/**
 * @file egl_headless.c
 * @brief Surfaceless EGL OpenGL context (see egl_headless.h).
 */
#include "ferrum/renderer/egl_headless.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdio.h>

static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLContext g_ctx = EGL_NO_CONTEXT;

void *egl_headless_getproc(const char *name, void *user)
{
    (void)user;
    return (void *)eglGetProcAddress(name);
}

void *egl_headless_getproc_glad(const char *name)
{
    return (void *)eglGetProcAddress(name);
}

bool egl_headless_init(int major, int minor)
{
    /* Prefer the surfaceless platform (no window system needed). Fall back to
     * the default display if the extension entry point is unavailable. */
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (get_platform_display != NULL)
        g_dpy = get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    if (g_dpy == EGL_NO_DISPLAY)
        g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_dpy == EGL_NO_DISPLAY) {
        fprintf(stderr, "egl_headless: no display\n");
        return false;
    }

    EGLint vmaj = 0, vmin = 0;
    if (!eglInitialize(g_dpy, &vmaj, &vmin)) {
        fprintf(stderr, "egl_headless: eglInitialize failed (0x%x)\n", eglGetError());
        return false;
    }
    if (!eglBindAPI(EGL_OPENGL_API)) {
        fprintf(stderr, "egl_headless: eglBindAPI(OpenGL) failed\n");
        return false;
    }

    const EGLint cfg_attrs[] = {
        EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE
    };
    EGLConfig cfg; EGLint n_cfg = 0;
    if (!eglChooseConfig(g_dpy, cfg_attrs, &cfg, 1, &n_cfg) || n_cfg < 1) {
        fprintf(stderr, "egl_headless: eglChooseConfig found no config\n");
        return false;
    }

    const EGLint ctx_attrs[] = {
        EGL_CONTEXT_MAJOR_VERSION,       major,
        EGL_CONTEXT_MINOR_VERSION,       minor,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_NONE
    };
    g_ctx = eglCreateContext(g_dpy, cfg, EGL_NO_CONTEXT, ctx_attrs);
    if (g_ctx == EGL_NO_CONTEXT) {
        fprintf(stderr, "egl_headless: eglCreateContext failed (0x%x)\n", eglGetError());
        return false;
    }
    /* Surfaceless: no draw/read surface (needs EGL_KHR_surfaceless_context). */
    if (!eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, g_ctx)) {
        fprintf(stderr, "egl_headless: eglMakeCurrent(surfaceless) failed (0x%x)\n", eglGetError());
        return false;
    }
    return true;
}

void egl_headless_shutdown(void)
{
    if (g_dpy == EGL_NO_DISPLAY)
        return;
    eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (g_ctx != EGL_NO_CONTEXT)
        eglDestroyContext(g_dpy, g_ctx);
    eglTerminate(g_dpy);
    g_ctx = EGL_NO_CONTEXT;
    g_dpy = EGL_NO_DISPLAY;
}

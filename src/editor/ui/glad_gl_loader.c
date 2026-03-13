/**
 * @file glad_gl_loader.c
 * @brief Bridge from SDL2/GLAD to the renderer's gl_loader_t abstraction.
 *
 * After gladLoadGLLoader() has been called, this provides a gl_loader_t
 * that resolves GL function pointers via SDL_GL_GetProcAddress.
 *
 * Non-static functions: 1 (glad_gl_loader_get).
 */

#include "ferrum/editor/ui/glad_gl_loader.h"
#include <SDL2/SDL.h>

/**
 * @brief SDL2-based get_proc_address callback for the renderer's gl_loader_t.
 */
static void *sdl_get_proc_(const char *name, void *user_data) {
    (void)user_data;
    return SDL_GL_GetProcAddress(name);
}

gl_loader_t glad_gl_loader_get(void) {
    gl_loader_t loader;
    loader.get_proc_address = sdl_get_proc_;
    loader.user_data = NULL;
    return loader;
}

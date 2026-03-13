/**
 * @file glad_gl_loader.h
 * @brief Bridge from GLAD to the renderer's gl_loader_t.
 *
 * Call glad_gl_loader_get() after gladLoadGLLoader() to obtain a
 * gl_loader_t usable with shader_program_create(), vao_create(), etc.
 *
 * Ownership: returns a stack value; no allocation.
 * Nullability: N/A.
 * Error semantics: assumes GLAD is already initialized.
 * Side effects: none.
 *
 * Public types: none (uses gl_loader_t from renderer).
 */
#ifndef FERRUM_EDITOR_UI_GLAD_GL_LOADER_H
#define FERRUM_EDITOR_UI_GLAD_GL_LOADER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ferrum/renderer/gl_loader.h"

/**
 * @brief Get a gl_loader_t backed by GLAD's loaded function pointers.
 *
 * Must be called after gladLoadGLLoader() succeeds.
 *
 * @return A gl_loader_t that resolves GL functions via GLAD.
 */
gl_loader_t glad_gl_loader_get(void);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_UI_GLAD_GL_LOADER_H */

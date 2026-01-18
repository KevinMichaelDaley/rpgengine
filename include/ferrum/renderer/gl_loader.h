#ifndef FERRUM_RENDERER_GL_LOADER_H
#define FERRUM_RENDERER_GL_LOADER_H

#include <stddef.h>
#include <stdint.h>

/** @file
 * @brief OpenGL function pointer loader table.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Status codes for GL loader validation. */
typedef enum gl_loader_status {
    GL_LOADER_OK = 0,
    GL_LOADER_ERR_MISSING = 1
} gl_loader_status_t;

/** OpenGL loader function pointer table. */
typedef struct gl_loader {
    void *(*get_proc_address)(const char *name, void *user_data);
    void *user_data;
} gl_loader_t;

/**
 * @brief Validate required GL function pointers are non-NULL.
 * @param loader Loader table (non-NULL).
 * @param out_missing Optional pointer to first missing symbol name (may be NULL).
 * @return GL_LOADER_OK on success or GL_LOADER_ERR_MISSING when a required entry is missing.
 */
gl_loader_status_t gl_loader_validate(const gl_loader_t *loader, const char **out_missing);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_GL_LOADER_H */

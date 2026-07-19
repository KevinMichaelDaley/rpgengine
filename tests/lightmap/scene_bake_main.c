/**
 * @file scene_bake_main.c
 * @brief Generic headless lightmap-bake harness. Owns main + the (optional EGL)
 *        GPU context and STB image implementation, and passes the GENERATED scene
 *        callback (@c scene_bake_setup, from an exported scene's scene_bake.c) to
 *        the bake driver. Build it with a generated scene_bake.c:
 *
 *   make build/scene_bake_egl SCENE_BAKE_C=datasets/great_hall_export/scene_bake.c
 *   HALL_GPU=1 build/scene_bake_egl <export_dir> <out.flm>
 *
 * With HALL_GPU the gather runs on the GPU (surfaceless EGL under SCENE_BAKE_EGL,
 * else a hidden SDL window); without it the driver bakes on the CPU.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glad/glad.h>
#ifdef SCENE_BAKE_EGL
#include "ferrum/renderer/egl_headless.h"
#else
#include <SDL2/SDL.h>
#endif
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "ferrum/lightmap/gpu/lm_gpu_gather.h"
#include "ferrum/lightmap/lm_bake_driver.h"
#include "ferrum/memory/arena.h"
#include "ferrum/renderer/gl_loader.h"

/* The generated scene callback (defined in the exported scene_bake.c TU). */
bool scene_bake_setup(lm_mesh_scene_t *scene, lm_bake_config_t *config,
                      arena_t *arena, void *user);

#ifndef SCENE_BAKE_EGL
static void *sb_getproc(const char *n, void *u) { (void)u; return SDL_GL_GetProcAddress(n); }
#endif

int main(int argc, char **argv)
{
    const char *root = argc > 1 ? argv[1] : ".";
    const char *out = argc > 2 ? argv[2] : "scene.flm";

    /* Big enough for high-res (HALL_LMRES>1) bakes: the per-luxel accum+de SH9
     * scratch scales with luxel count, and at 2x res the old 1.8 GB arena
     * overflowed -- arena_alloc then returned NULL and the GI+SDF gather block
     * was silently skipped (a valid but UNLIT .flm with no SDF chunks). Heap-
     * allocated (not a static array) so it can exceed the 2 GB small-code-model
     * .bss relocation limit; overridable via SCENE_BAKE_ARENA_MB. One-shot bake
     * tool, so a single malloc at startup is fine. */
    size_t arena_mb = getenv("SCENE_BAKE_ARENA_MB")
                    ? (size_t)strtoull(getenv("SCENE_BAKE_ARENA_MB"), NULL, 10)
                    : 8192ull;
    size_t abuf_size = arena_mb * 1024ull * 1024ull;
    char *abuf = malloc(abuf_size);
    if (!abuf) { fprintf(stderr, "arena malloc(%zu MB) failed\n", arena_mb); return 1; }
    arena_t arena;
    arena_init(&arena, abuf, abuf_size);

    gl_loader_t gl_loader;
    const gl_loader_t *gl = NULL;
    if (getenv("HALL_GPU")) {
#ifdef SCENE_BAKE_EGL
        if (!egl_headless_init(4, 3)) { fprintf(stderr, "egl init failed\n"); return 1; }
        if (!gladLoadGLLoader((GLADloadproc)egl_headless_getproc_glad)) {
            fprintf(stderr, "glad failed\n"); return 1; }
        gl_loader = (gl_loader_t){ egl_headless_getproc, NULL };
#else
        if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL init failed\n"); return 1; }
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_Window *w = SDL_CreateWindow("scenebake", 0, 0, 16, 16,
                                         SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        SDL_GLContext c = SDL_GL_CreateContext(w);
        SDL_GL_MakeCurrent(w, c); (void)c;
        if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
            fprintf(stderr, "glad failed\n"); return 1; }
        gl_loader = (gl_loader_t){ sb_getproc, NULL };
#endif
        gl = &gl_loader;
        printf("GPU gather ENABLED: %s\n", (const char *)glGetString(GL_RENDERER));
        fflush(stdout);
    }

    if (!lm_bake_driver_run(gl, scene_bake_setup, (void *)root, out, &arena)) {
        fprintf(stderr, "bake failed\n");
        return 1;
    }
    printf("wrote %s\n", out);
    return 0;
}

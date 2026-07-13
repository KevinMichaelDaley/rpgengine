/**
 * @file p004_renderer_pbr_shader_tests.c
 * @brief Unit test: the core PBR shader compiles and links on a real context.
 */
#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <stdio.h>

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/pbr_shader.h"

static void *sdl_get_proc(const char *name, void *user) { (void)user; return SDL_GL_GetProcAddress(name); }

int main(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "no display; skip\n"); return 0; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window *w = SDL_CreateWindow("pbr", 0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!w) { SDL_Quit(); return 0; }
    SDL_GLContext c = SDL_GL_CreateContext(w);
    SDL_GL_MakeCurrent(w, c);
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) { SDL_Quit(); return 0; }

    gl_loader_t loader = { sdl_get_proc, NULL };
    shader_program_t prog;
    char log[2048] = { 0 };
    shader_program_status_t st = pbr_shader_create(&prog, &loader, log, sizeof(log));
    int failed = 0;
    if (st != SHADER_PROGRAM_OK) {
        fprintf(stderr, "FAIL pbr_shader_create: %s\n", log);
        failed = 1;
    } else if (shader_program_handle(&prog) == 0u) {
        fprintf(stderr, "FAIL zero program handle\n");
        failed = 1;
    } else {
        printf("OK pbr_shader compiles+links\n");
        shader_program_destroy(&prog);
    }
    SDL_GL_DeleteContext(c);
    SDL_DestroyWindow(w);
    SDL_Quit();
    printf("%s\n", failed ? "FAILED" : "PASSED");
    return failed;
}

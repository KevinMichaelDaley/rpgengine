/**
 * @file p004_renderer_depth_prepass_tests.c
 * @brief Unit test: the depth pre-pass compiles, runs GL-clean, and writes depth.
 */
#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <stdio.h>
#include <string.h>

#include "ferrum/renderer/depth_prepass.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/material.h"
#include "ferrum/renderer/mesh/static_mesh.h"
#include "ferrum/renderer/render_camera.h"
#include "ferrum/renderer/render_scene.h"

#ifndef GL_DEPTH_COMPONENT
#define GL_DEPTH_COMPONENT 0x1902
#endif

static void *sdl_get_proc(const char *n, void *u) { (void)u; return SDL_GL_GetProcAddress(n); }

int main(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr,"no display; skip\n"); return 0; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window *w = SDL_CreateWindow("depth_t", 0, 0, 128, 128, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!w) { SDL_Quit(); return 0; }
    SDL_GLContext c = SDL_GL_CreateContext(w);
    SDL_GL_MakeCurrent(w, c);
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) { SDL_Quit(); return 0; }
    (void)glGetError();
    gl_loader_t loader = { sdl_get_proc, NULL };

    int failed = 0;
    depth_prepass_t pass;
    if (depth_prepass_init(&pass, &loader) != SHADER_PROGRAM_OK) { fprintf(stderr,"FAIL init\n"); return 1; }

    static_mesh_t sphere; static_mesh_create_sphere(&loader, 1.0f, 24, 16, &sphere);
    render_material_t mat; material_init(&mat);
    render_renderable_t bk[1]; render_scene_t scene; render_scene_init(&scene, bk, 1);
    float m[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    render_scene_add(&scene, &sphere, &mat, m);
    float eye[3]={0,0,4}, tgt[3]={0,0,0}, up[3]={0,1,0};
    render_camera_look_at(&scene.camera, eye, tgt, up, 1.0f, 1.0f, 0.1f, 50.0f);

    glViewport(0,0,128,128);
    glClearDepth(1.0); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    depth_prepass_execute(&pass, &scene);

    if (glGetError() != GL_NO_ERROR) { fprintf(stderr,"FAIL gl error\n"); failed = 1; }
    float d = 1.0f;
    glReadPixels(64, 64, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &d);
    if (!(d < 1.0f)) { fprintf(stderr,"FAIL centre depth not written (%f)\n", d); failed = 1; }
    else printf("OK centre depth = %f (< 1.0)\n", d);

    static_mesh_destroy(&sphere);
    depth_prepass_destroy(&pass);
    SDL_GL_DeleteContext(c); SDL_DestroyWindow(w); SDL_Quit();
    printf("%s\n", failed ? "FAILED" : "PASSED");
    return failed;
}

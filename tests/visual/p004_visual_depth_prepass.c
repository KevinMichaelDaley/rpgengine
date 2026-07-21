/**
 * @file p004_visual_depth_prepass.c
 * @brief Visual test of the depth pre-pass: run it on depth-staggered spheres
 *        and visualise the resulting depth buffer as grayscale (nearer=brighter).
 */
#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/renderer/depth_prepass.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/material.h"
#include "ferrum/renderer/mesh/static_mesh.h"
#include "ferrum/renderer/render_camera.h"
#include "ferrum/renderer/render_scene.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef GL_DEPTH_COMPONENT
#define GL_DEPTH_COMPONENT 0x1902
#endif
#define WIN 640

static void *sdl_get_proc(const char *n, void *u) { (void)u; return SDL_GL_GetProcAddress(n); }
static void tr(float m[16], float x, float y, float z) {
    memset(m, 0, 16*sizeof(float)); m[0]=m[5]=m[10]=m[15]=1; m[12]=x; m[13]=y; m[14]=z;
}

/* Read the depth buffer and write it as a grayscale PPM (nearest = brightest). */
static void save_depth_ppm(const char *path, int w, int h) {
    float *d = malloc((size_t)w*h*sizeof(float));
    if (!d) return;
    glReadPixels(0, 0, w, h, GL_DEPTH_COMPONENT, GL_FLOAT, d);
    float dmin = 1.0f;
    for (int i=0;i<w*h;i++) if (d[i] < dmin) dmin = d[i];
    uint8_t *rgb = malloc((size_t)w*h*3);
    for (int i=0;i<w*h;i++) {
        uint8_t v = 0;
        if (d[i] < 0.99999f) { float t = 1.0f - (d[i]-dmin)/(1.0f-dmin+1e-6f); v=(uint8_t)(t*255.0f); }
        rgb[i*3]=rgb[i*3+1]=rgb[i*3+2]=v;
    }
    FILE *f = fopen(path,"wb");
    if (f){ fprintf(f,"P6\n%d %d\n255\n",w,h);
        for (int y=h-1;y>=0;--y) fwrite(rgb+(size_t)y*w*3,1,(size_t)w*3,f);
        fclose(f); printf("screenshot: %s\n", path); }
    free(rgb); free(d);
}

int main(int argc, char **argv) {
    const char *shot = argc > 1 ? argv[1] : "/tmp/p004_depth.ppm";
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window *win = SDL_CreateWindow("depth", 0, 0, WIN, WIN, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    SDL_GLContext gc = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, gc);
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) return 1;
    gl_loader_t loader = { sdl_get_proc, NULL };

    depth_prepass_t pass;
    if (depth_prepass_init(&pass, &loader) != SHADER_PROGRAM_OK) { fprintf(stderr,"prepass init\n"); return 1; }

    static_mesh_t sphere; static_mesh_create_sphere(&loader, 1.0f, 40, 26, &sphere);
    render_material_t mat; material_init(&mat);
    render_renderable_t backing[4]; render_scene_t scene; render_scene_init(&scene, backing, 4);
    float m[16];
    tr(m,-2.5f,0,0);   render_scene_add(&scene,&sphere,&mat,m);  /* near */
    tr(m,0,0,-3.0f);   render_scene_add(&scene,&sphere,&mat,m);  /* mid */
    tr(m,2.8f,0,-6.0f);render_scene_add(&scene,&sphere,&mat,m);  /* far */
    float eye[3]={0,0.5f,10}, tgt[3]={0,0,-3}, up[3]={0,1,0};
    render_camera_look_at(&scene.camera, eye, tgt, up, 45.0f*(float)M_PI/180.0f, 1.0f, 0.5f, 40.0f);

    glViewport(0,0,WIN,WIN);
    for (int frame=0; frame<3; ++frame) {
        glClearColor(0,0,0,1);
        glClearDepth(1.0); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        depth_prepass_execute(&pass, &scene, 0.0f);
        if (frame==1) save_depth_ppm(shot, WIN, WIN);
        SDL_GL_SwapWindow(win); SDL_Delay(60);
    }
    static_mesh_destroy(&sphere);
    depth_prepass_destroy(&pass);
    SDL_GL_DeleteContext(gc); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}

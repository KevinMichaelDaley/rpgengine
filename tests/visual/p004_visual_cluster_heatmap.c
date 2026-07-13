/**
 * @file p004_visual_cluster_heatmap.c
 * @brief Visual test of froxel clustering: build the cluster grid for a set of
 *        point lights and draw a screen-tile heatmap of the per-tile light
 *        count (max over depth slices). Hot cells align with the lights.
 */
#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/renderer/cluster_grid.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/light.h"
#include "ferrum/renderer/render_camera.h"
#include "ferrum/renderer/shader_program.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define WIN 720
#define TX 24
#define TY 24
#define TS 16
#define NCL (TX * TY * TS)

static void *sdl_get_proc(const char *n, void *u) { (void)u; return SDL_GL_GetProcAddress(n); }

static void save_ppm(const char *path, int w, int h) {
    size_t row = (size_t)w * 3; uint8_t *rgb = malloc(row * (size_t)h);
    if (!rgb) return;
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    FILE *f = fopen(path, "wb");
    if (f) { fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int y = h - 1; y >= 0; --y) fwrite(rgb + (size_t)y * row, 1, row, f);
        fclose(f); printf("screenshot: %s\n", path); }
    free(rgb);
}

static render_light_t pl(float x, float y, float z, float r) {
    render_light_t l; memset(&l, 0, sizeof(l));
    l.kind = RENDER_LIGHT_POINT; l.position[0]=x; l.position[1]=y; l.position[2]=z;
    l.range=r; l.flags=RENDER_LIGHT_FLAG_REALTIME; return l;
}

int main(int argc, char **argv) {
    const char *shot = argc > 1 ? argv[1] : "/tmp/p004_cluster.ppm";

    /* Camera looking down -z; a handful of point lights across the view. */
    render_camera_t cam;
    float eye[3]={0,0,0}, tgt[3]={0,0,-1}, up[3]={0,1,0};
    render_camera_look_at(&cam, eye, tgt, up, 60.0f*(float)M_PI/180.0f, 1.0f, 0.5f, 60.0f);
    render_light_t lights[7] = {
        pl(-4, 3, -10, 2.5f), pl(5, -3, -12, 3.0f), pl(0, 0, -8, 2.0f),
        pl(-6, -5, -18, 4.0f), pl(7, 6, -22, 5.0f), pl(2, 2, -14, 2.5f),
        pl(-2, -1, -9, 2.0f),
    };
    uint32_t off[NCL], cnt[NCL], idx[NCL*8];
    cluster_grid_t g;
    cluster_grid_init(&g, (cluster_config_t){ TX,TY,TS, 0.5f,60.0f }, off, cnt, idx, NCL*8);
    cluster_grid_build(&g, &cam, lights, 7);

    /* Per-tile heat = max light count over depth slices. */
    uint32_t maxc = 1;
    static uint32_t tile_heat[TX*TY];
    for (int ty=0; ty<TY; ++ty) for (int tx=0; tx<TX; ++tx) {
        uint32_t m = 0;
        for (int s=0; s<TS; ++s) { uint32_t c = cnt[cluster_grid_index(&g,tx,ty,s)]; if (c>m) m=c; }
        tile_heat[ty*TX+tx] = m; if (m>maxc) maxc=m;
    }
    printf("cluster indices used: %u; peak tile count: %u\n", g.index_count, maxc);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window *win = SDL_CreateWindow("cluster", 0, 0, WIN, WIN, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    SDL_GLContext gc = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, gc);
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) return 1;
    gl_loader_t loader = { sdl_get_proc, NULL };

    const char *vs = "#version 330 core\nin vec2 p; in vec3 col; out vec3 v;\n"
        "void main(){ v=col; gl_Position=vec4(p,0,1); }\n";
    const char *fs = "#version 330 core\nin vec3 v; out vec4 o; void main(){ o=vec4(v,1.0); }\n";
    shader_program_t prog; char log[512]={0};
    shader_program_create(&prog, &loader, vs, fs, log, sizeof(log));
    GLuint ph=(GLuint)shader_program_handle(&prog);
    GLint a_p=glGetAttribLocation(ph,"p"), a_c=glGetAttribLocation(ph,"col");

    /* Build a coloured quad per tile (2 tris * 5 floats). */
    static float verts[TX*TY*6*5];
    int vi=0;
    for (int ty=0; ty<TY; ++ty) for (int tx=0; tx<TX; ++tx) {
        float t = (float)tile_heat[ty*TX+tx] / (float)maxc;
        float cr=t, cg=1.0f-fabsf(2.0f*t-1.0f), cb=1.0f-t;
        if (tile_heat[ty*TX+tx]==0){ cr=cg=0.02f; cb=0.06f; }
        float x0=(float)tx/TX*2-1, x1=(float)(tx+1)/TX*2-1;
        float y0=(float)ty/TY*2-1, y1=(float)(ty+1)/TY*2-1;
        float g0=0.006f; /* tiny gutter */
        float quad[6][2]={{x0+g0,y0+g0},{x1-g0,y0+g0},{x1-g0,y1-g0},{x0+g0,y0+g0},{x1-g0,y1-g0},{x0+g0,y1-g0}};
        for (int k=0;k<6;k++){ float *p=&verts[vi*5]; p[0]=quad[k][0]; p[1]=quad[k][1]; p[2]=cr; p[3]=cg; p[4]=cb; ++vi; }
    }
    GLuint vao,vbo; glGenVertexArrays(1,&vao); glGenBuffers(1,&vbo);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(verts),verts,GL_STATIC_DRAW);
    glEnableVertexAttribArray((GLuint)a_p); glVertexAttribPointer((GLuint)a_p,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)0);
    glEnableVertexAttribArray((GLuint)a_c); glVertexAttribPointer((GLuint)a_c,3,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(2*sizeof(float)));

    glViewport(0,0,WIN,WIN);
    for (int frame=0; frame<3; ++frame) {
        glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
        shader_program_bind(&prog); glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES,0,vi);
        if (frame==1) save_ppm(shot,WIN,WIN);
        SDL_GL_SwapWindow(win); SDL_Delay(60);
    }
    shader_program_destroy(&prog);
    SDL_GL_DeleteContext(gc); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}

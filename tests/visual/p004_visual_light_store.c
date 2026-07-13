/**
 * @file p004_visual_light_store.c
 * @brief Visual test: build a scene light store, pack it, and drive the PBR
 *        shader's punctual light arrays from it to shade spheres. Screenshot.
 */
#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/light.h"
#include "ferrum/renderer/light_store.h"
#include "ferrum/renderer/material.h"
#include "ferrum/renderer/pbr_shader.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define WIN 768
#define NSPHERE 5

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

static uint32_t gen_sphere(float *out, uint32_t cap, int stacks, int slices) {
    uint32_t v = 0;
    for (int i = 0; i < stacks; ++i) for (int j = 0; j < slices; ++j) for (int k = 0; k < 6; ++k) {
        int di = (k == 2 || k == 4 || k == 5) ? 1 : 0, dj = (k == 1 || k == 2 || k == 4) ? 1 : 0;
        float th = (float)(i + di) / stacks * (float)M_PI, ph = (float)(j + dj) / slices * 2.0f * (float)M_PI;
        float st = sinf(th), ct = cosf(th), sp = sinf(ph), cp = cosf(ph);
        if (v >= cap) return v;
        float *p = &out[v * 14];
        p[0]=st*cp; p[1]=ct; p[2]=st*sp; p[3]=p[0]; p[4]=p[1]; p[5]=p[2];
        p[6]=-st*sp; p[7]=0; p[8]=st*cp; p[9]=1; p[10]=(float)(j+dj)/slices; p[11]=(float)(i+di)/stacks;
        p[12]=p[10]; p[13]=p[11]; ++v;
    }
    return v;
}

static render_light_t point_light(float x, float y, float z, float r, float g, float b, float ix) {
    render_light_t l; memset(&l, 0, sizeof(l));
    l.kind = RENDER_LIGHT_POINT;
    l.position[0]=x; l.position[1]=y; l.position[2]=z;
    l.color[0]=r; l.color[1]=g; l.color[2]=b; l.intensity=ix; l.range=22.0f;
    l.flags = RENDER_LIGHT_FLAG_REALTIME;
    return l;
}

int main(int argc, char **argv) {
    const char *shot = argc > 1 ? argv[1] : "/tmp/p004_light_store.ppm";

    /* Build a scene light store: 3 realtime point lights + 1 baked-only (must
     * NOT appear in the render, proving the pack filter). */
    render_light_t backing[8];
    render_light_store_t store;
    render_light_store_init(&store, backing, 8);
    render_light_add(&store, &(render_light_t){ 0 }); store.count = 0; /* reset check */
    render_light_t a = point_light(-4, 2.5f, 4, 14, 2, 2, 1.0f);
    render_light_t b = point_light(4, 2.5f, 4, 2, 14, 3, 1.0f);
    render_light_t c = point_light(0, -2.5f, 5, 2, 3, 14, 1.0f);
    render_light_t baked = point_light(0, 5, 0, 40, 40, 40, 1.0f);
    baked.flags = RENDER_LIGHT_FLAG_BAKED; /* filtered out by pack */
    render_light_add(&store, &a); render_light_add(&store, &b);
    render_light_add(&store, &c); render_light_add(&store, &baked);

    int32_t ltype[32]; float lpos[96], ldir[96], lcol[96], lrange[32], lci[32], lco[32];
    uint32_t nl = render_light_store_pack(&store, ltype, lpos, ldir, lcol, lrange, lci, lco, 32);
    printf("packed %u realtime lights (of %u in store)\n", nl, store.count);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window *win = SDL_CreateWindow("light_store", 0, 0, WIN, WIN, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    SDL_GLContext gc = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, gc);
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) return 1;
    gl_loader_t loader = { sdl_get_proc, NULL };

    shader_program_t prog; char log[2048] = { 0 };
    if (pbr_shader_create(&prog, &loader, log, sizeof(log)) != SHADER_PROGRAM_OK) {
        fprintf(stderr, "shader: %s\n", log); return 1; }
    shader_uniform_cache_t cache; shader_uniform_cache_init(&cache, &prog);
    GLuint ph = (GLuint)shader_program_handle(&prog);

    int stacks = 32, slices = 48; uint32_t cap = (uint32_t)(stacks * slices * 6);
    float *verts = malloc((size_t)cap * 14 * sizeof(float));
    uint32_t vcount = gen_sphere(verts, cap, stacks, slices);
    GLuint vao, vbo; glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vcount * 14 * sizeof(float)), verts, GL_STATIC_DRAW);
    int comps[5]={3,3,4,2,2}; size_t off[5]={0,3,6,10,12};
    for (int aa=0;aa<5;aa++){ glEnableVertexAttribArray((GLuint)aa);
        glVertexAttribPointer((GLuint)aa, comps[aa], GL_FLOAT, GL_FALSE, 14*sizeof(float), (void*)(off[aa]*sizeof(float))); }
    free(verts);

    mat4_t proj, view;
    mat4_perspective(46.0f*(float)M_PI/180.0f, 1.0f, 0.1f, 100.0f, &proj);
    mat4_look_at((vec3_t){0,0,16.5f}, (vec3_t){0,0,0}, (vec3_t){0,1,0}, &view);

    glEnable(GL_DEPTH_TEST); glViewport(0,0,WIN,WIN);
    for (int frame=0; frame<3; ++frame) {
        glClearColor(0.03f,0.03f,0.05f,1.0f); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        shader_program_bind(&prog);
        shader_uniform_set_mat4(&cache,&prog,"u_view",view.m,0);
        shader_uniform_set_mat4(&cache,&prog,"u_projection",proj.m,0);
        float eye[3]={0,0,16.5f}, zero[3]={0,0,0}, amb[3]={0.02f,0.02f,0.03f};
        shader_uniform_set_vec3(&cache,&prog,"u_eye_pos",eye);
        shader_uniform_set_vec3(&cache,&prog,"u_sun_color",zero);
        shader_uniform_set_vec3(&cache,&prog,"u_sun_dir",eye);
        shader_uniform_set_vec3(&cache,&prog,"u_ambient",amb);
        shader_uniform_set_int(&cache,&prog,"u_sh_enabled",0);
        glUniform1i(glGetUniformLocation(ph,"u_light_count"), (int)nl);
        glUniform1iv(glGetUniformLocation(ph,"u_light_type"), (int)nl, ltype);
        glUniform3fv(glGetUniformLocation(ph,"u_light_pos"), (int)nl, lpos);
        glUniform3fv(glGetUniformLocation(ph,"u_light_dir"), (int)nl, ldir);
        glUniform3fv(glGetUniformLocation(ph,"u_light_color"), (int)nl, lcol);
        glUniform1fv(glGetUniformLocation(ph,"u_light_range"), (int)nl, lrange);
        glUniform1fv(glGetUniformLocation(ph,"u_light_cos_inner"), (int)nl, lci);
        glUniform1fv(glGetUniformLocation(ph,"u_light_cos_outer"), (int)nl, lco);
        glBindVertexArray(vao);
        for (int s=0;s<NSPHERE;s++){
            render_material_t m; material_init(&m);
            m.roughness_min = m.roughness_max = 0.08f + (float)s/(NSPHERE-1)*0.5f;
            m.tint[0]=m.tint[1]=m.tint[2]=0.85f;
            material_bind(&m,0u,&cache,&prog);
            float tx=((float)s-(NSPHERE-1)/2.0f)*2.4f;
            mat4_t model=mat4_identity(); model.m[12]=tx;
            shader_uniform_set_mat4(&cache,&prog,"u_model",model.m,0);
            glDrawArrays(GL_TRIANGLES,0,(GLsizei)vcount);
        }
        if (frame==1) save_ppm(shot, WIN, WIN);
        SDL_GL_SwapWindow(win); SDL_Delay(60);
    }
    shader_program_destroy(&prog);
    SDL_GL_DeleteContext(gc); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}

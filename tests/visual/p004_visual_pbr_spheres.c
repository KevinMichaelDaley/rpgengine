/**
 * @file p004_visual_pbr_spheres.c
 * @brief Visual validation of the PBR BRDF: a grid of spheres sweeping
 *        roughness (columns) across dielectric (top) and metal (bottom) rows,
 *        under one directional light. Writes a PPM screenshot.
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
#include "ferrum/renderer/material.h"
#include "ferrum/renderer/pbr_shader.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define WIN 768
#define COLS 6
#define ROWS 2

static void *sdl_get_proc(const char *name, void *user) { (void)user; return SDL_GL_GetProcAddress(name); }

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

/* Build a non-indexed UV sphere: 14 floats/vertex (pos3,normal3,tangent4,uv0_2,uv1_2). */
static uint32_t gen_sphere(float *out, uint32_t cap_verts, int stacks, int slices) {
    uint32_t v = 0;
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            for (int k = 0; k < 6; ++k) {
                int di = (k == 2 || k == 4 || k == 5) ? 1 : 0;
                int dj = (k == 1 || k == 2 || k == 4) ? 1 : 0;
                int si = i + di, sj = j + dj;
                float th = (float)si / (float)stacks * (float)M_PI;
                float ph = (float)sj / (float)slices * 2.0f * (float)M_PI;
                float st = sinf(th), ct = cosf(th), sp = sinf(ph), cp = cosf(ph);
                if (v >= cap_verts) return v;
                float *p = &out[v * 14];
                p[0] = st * cp; p[1] = ct; p[2] = st * sp;      /* pos (unit) */
                p[3] = p[0]; p[4] = p[1]; p[5] = p[2];          /* normal */
                p[6] = -st * sp; p[7] = 0.0f; p[8] = st * cp;   /* tangent xyz */
                p[9] = 1.0f;                                    /* tangent w */
                p[10] = (float)sj / (float)slices; p[11] = (float)si / (float)stacks;
                p[12] = p[10]; p[13] = p[11];                   /* uv1 = uv0 */
                ++v;
            }
        }
    }
    return v;
}

static void set_vec3(shader_uniform_cache_t *c, shader_program_t *p, const char *n, float x, float y, float z) {
    float v[3] = { x, y, z }; shader_uniform_set_vec3(c, p, n, v);
}

int main(int argc, char **argv) {
    const char *shot = argc > 1 ? argv[1] : "/tmp/p004_pbr_spheres.ppm";
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window *win = SDL_CreateWindow("pbr_spheres", 0, 0, WIN, WIN, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    SDL_GLContext gc = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, gc);
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) return 1;
    gl_loader_t loader = { sdl_get_proc, NULL };

    shader_program_t prog; char log[2048] = { 0 };
    if (pbr_shader_create(&prog, &loader, log, sizeof(log)) != SHADER_PROGRAM_OK) {
        fprintf(stderr, "shader: %s\n", log); return 1; }
    shader_uniform_cache_t cache; shader_uniform_cache_init(&cache, &prog);

    int stacks = 32, slices = 48;
    uint32_t cap = (uint32_t)(stacks * slices * 6);
    float *verts = malloc((size_t)cap * 14 * sizeof(float));
    uint32_t vcount = gen_sphere(verts, cap, stacks, slices);
    GLuint vao, vbo; glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)vcount * 14 * sizeof(float)), verts, GL_STATIC_DRAW);
    size_t st = 14 * sizeof(float);
    GLint locs[5] = { 0, 1, 2, 3, 4 };
    int comps[5] = { 3, 3, 4, 2, 2 }; size_t off[5] = { 0, 3, 6, 10, 12 };
    for (int a = 0; a < 5; ++a) {
        glEnableVertexAttribArray((GLuint)locs[a]);
        glVertexAttribPointer((GLuint)locs[a], comps[a], GL_FLOAT, GL_FALSE, (GLsizei)st, (void *)(off[a] * sizeof(float)));
    }
    free(verts);

    mat4_t proj, view;
    mat4_perspective(46.0f * (float)M_PI / 180.0f, 1.0f, 0.1f, 100.0f, &proj);
    mat4_look_at((vec3_t){ 0, 0, 16.5f }, (vec3_t){ 0, 0, 0 }, (vec3_t){ 0, 1, 0 }, &view);

    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, WIN, WIN);
    for (int frame = 0; frame < 3; ++frame) {
        glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        shader_program_bind(&prog);
        shader_uniform_set_mat4(&cache, &prog, "u_view", view.m, 0);
        shader_uniform_set_mat4(&cache, &prog, "u_projection", proj.m, 0);
        set_vec3(&cache, &prog, "u_eye_pos", 0, 0, 16.5f);
        set_vec3(&cache, &prog, "u_sun_dir", 0.4f, 0.5f, 1.0f);
        set_vec3(&cache, &prog, "u_sun_color", 3.0f, 2.9f, 2.7f);
        set_vec3(&cache, &prog, "u_ambient", 0.03f, 0.03f, 0.04f);
        glBindVertexArray(vao);
        for (int r = 0; r < ROWS; ++r) {
            for (int col = 0; col < COLS; ++col) {
                render_material_t m; material_init(&m);
                float rough = 0.05f + (float)col / (float)(COLS - 1) * 0.95f;
                m.roughness_min = m.roughness_max = rough;
                m.metalness = (r == 1) ? 1.0f : 0.0f;
                if (r == 1) { m.tint[0] = 1.0f; m.tint[1] = 0.78f; m.tint[2] = 0.34f; } /* gold */
                else { m.tint[0] = m.tint[1] = m.tint[2] = 0.85f; }                     /* dielectric */
                material_bind(&m, 0u, &cache, &prog);
                float tx = ((float)col - (COLS - 1) / 2.0f) * 1.95f;
                float ty = ((float)r - (ROWS - 1) / 2.0f) * -2.6f;
                mat4_t model = mat4_identity();
                model.m[0] = model.m[5] = model.m[10] = 1.0f; /* radius */
                model.m[12] = tx; model.m[13] = ty; model.m[14] = 0.0f;
                shader_uniform_set_mat4(&cache, &prog, "u_model", model.m, 0);
                glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vcount);
            }
        }
        if (frame == 1) save_ppm(shot, WIN, WIN);
        SDL_GL_SwapWindow(win);
        SDL_Delay(60);
    }
    shader_program_destroy(&prog);
    SDL_GL_DeleteContext(gc); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}

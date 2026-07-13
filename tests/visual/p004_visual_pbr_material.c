/**
 * @file p004_visual_pbr_material.c
 * @brief Full-material PBR test: a lit quad shaded with the complete brick
 *        material (albedo + tangent-space normal + AO maps) through the shader
 *        wrapper, showing normal-mapped relief and AO. PPM screenshot.
 */
#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/material.h"
#include "ferrum/renderer/pbr_shader.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"
#include "ferrum/renderer/texture.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define WIN 700

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

/* Load a PNG into a texture with the given format (mips, repeat). */
static int load_tex(texture_t *t, const gl_loader_t *loader, const char *path, texture_format_t fmt) {
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 3);
    if (!px) { fprintf(stderr, "stbi_load %s\n", path); return 0; }
    texture_create(t, loader);
    texture_upload_2d(t, fmt, (uint32_t)w, (uint32_t)h, px, true);
    texture_set_sampler(t, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_REPEAT, GL_REPEAT);
    stbi_image_free(px);
    return 1;
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "assets/arch/proc/prefabs/bake";
    const char *shot = argc > 2 ? argv[2] : "/tmp/p004_pbr_material.ppm";
    char albedo_p[256], normal_p[256], ao_p[256];
    snprintf(albedo_p, sizeof(albedo_p), "%s/tint.png", dir);
    snprintf(normal_p, sizeof(normal_p), "%s/normal.png", dir);
    snprintf(ao_p, sizeof(ao_p), "%s/ao.png", dir);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window *win = SDL_CreateWindow("pbr_material", 0, 0, WIN, WIN, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    SDL_GLContext gc = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, gc);
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) return 1;
    gl_loader_t loader = { sdl_get_proc, NULL };

    /* Full material: albedo (sRGB), tangent-space normal (linear), AO (linear). */
    texture_t albedo, normal, ao;
    load_tex(&albedo, &loader, albedo_p, TEXTURE_FORMAT_SRGB8);
    load_tex(&normal, &loader, normal_p, TEXTURE_FORMAT_RGB8);
    load_tex(&ao, &loader, ao_p, TEXTURE_FORMAT_RGB8);
    render_material_t mat; material_init(&mat);
    mat.maps[MATERIAL_TEX_ALBEDO] = &albedo;
    mat.maps[MATERIAL_TEX_NORMAL] = &normal;
    mat.maps[MATERIAL_TEX_AO] = &ao;
    mat.roughness_min = 0.35f; mat.roughness_max = 0.75f; /* no roughness map: use base */
    mat.metalness = 0.0f; mat.normal_scale = 1.4f; mat.ao_strength = 1.0f;

    shader_program_t prog; char log[2048] = { 0 };
    if (pbr_shader_create(&prog, &loader, log, sizeof(log)) != SHADER_PROGRAM_OK) {
        fprintf(stderr, "shader: %s\n", log); return 1; }
    shader_uniform_cache_t cache; shader_uniform_cache_init(&cache, &prog);
    GLuint ph = (GLuint)shader_program_handle(&prog);

    /* Quad in XY plane (z=0), normal +z, tangent +x, uv0 [0,1] (aspect-matched). */
    float ar = 2048.0f / 1184.0f, hw = 3.0f, hh = hw / ar;
    float q[] = {
        -hw,-hh,0,  0,0,1,  1,0,0,1,  0,1, 0,1,
         hw,-hh,0,  0,0,1,  1,0,0,1,  1,1, 1,1,
         hw, hh,0,  0,0,1,  1,0,0,1,  1,0, 1,0,
        -hw,-hh,0,  0,0,1,  1,0,0,1,  0,1, 0,1,
         hw, hh,0,  0,0,1,  1,0,0,1,  1,0, 1,0,
        -hw, hh,0,  0,0,1,  1,0,0,1,  0,0, 0,0,
    };
    GLuint vao, vbo; glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STATIC_DRAW);
    int comps[5]={3,3,4,2,2}; size_t off[5]={0,3,6,10,12};
    for (int a=0;a<5;a++){ glEnableVertexAttribArray((GLuint)a);
        glVertexAttribPointer((GLuint)a, comps[a], GL_FLOAT, GL_FALSE, 14*sizeof(float), (void*)(off[a]*sizeof(float))); }

    mat4_t proj, view, model = mat4_identity();
    mat4_perspective(42.0f*(float)M_PI/180.0f, 1.0f, 0.1f, 100.0f, &proj);
    mat4_look_at((vec3_t){0,0,7.5f}, (vec3_t){0,0,0}, (vec3_t){0,1,0}, &view);

    glEnable(GL_DEPTH_TEST); glViewport(0,0,WIN,WIN);
    for (int frame=0; frame<3; ++frame) {
        glClearColor(0.05f,0.05f,0.06f,1.0f); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        shader_program_bind(&prog);
        shader_uniform_set_mat4(&cache,&prog,"u_view",view.m,0);
        shader_uniform_set_mat4(&cache,&prog,"u_projection",proj.m,0);
        shader_uniform_set_mat4(&cache,&prog,"u_model",model.m,0);
        float eye[3]={0,0,7.5f}, zero[3]={0,0,0}, amb[3]={0.04f,0.04f,0.05f};
        shader_uniform_set_vec3(&cache,&prog,"u_eye_pos",eye);
        shader_uniform_set_vec3(&cache,&prog,"u_sun_color",zero);
        shader_uniform_set_vec3(&cache,&prog,"u_sun_dir",eye);
        shader_uniform_set_vec3(&cache,&prog,"u_ambient",amb);
        shader_uniform_set_int(&cache,&prog,"u_sh_enabled",0);
        /* One grazing point light (upper-left) to reveal the normal-mapped relief. */
        glUniform1i(glGetUniformLocation(ph,"u_light_count"), 1);
        int lt=0; float lp[3]={-3.2f,2.6f,2.2f}, ld[3]={0,0,-1}, lc[3]={10,9.4f,8.6f}, lr=18, lci=0, lco=0;
        glUniform1iv(glGetUniformLocation(ph,"u_light_type"),1,&lt);
        glUniform3fv(glGetUniformLocation(ph,"u_light_pos"),1,lp);
        glUniform3fv(glGetUniformLocation(ph,"u_light_dir"),1,ld);
        glUniform3fv(glGetUniformLocation(ph,"u_light_color"),1,lc);
        glUniform1fv(glGetUniformLocation(ph,"u_light_range"),1,&lr);
        glUniform1fv(glGetUniformLocation(ph,"u_light_cos_inner"),1,&lci);
        glUniform1fv(glGetUniformLocation(ph,"u_light_cos_outer"),1,&lco);
        material_bind(&mat, 0u, &cache, &prog);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        if (frame==1) save_ppm(shot, WIN, WIN);
        SDL_GL_SwapWindow(win); SDL_Delay(60);
    }
    texture_destroy(&albedo); texture_destroy(&normal); texture_destroy(&ao);
    shader_program_destroy(&prog);
    SDL_GL_DeleteContext(gc); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}

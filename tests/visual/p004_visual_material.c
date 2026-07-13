/**
 * @file p004_visual_material.c
 * @brief Visual smoke test for render_material_t: bind a material (albedo bake +
 *        tint) through the uniform contract and render a quad; screenshot.
 */
#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/material.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"
#include "ferrum/renderer/texture.h"

#define WIN 512

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

static texture_t load_srgb(const gl_loader_t *loader, const char *path) {
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 3);
    texture_t t; texture_create(&t, loader);
    if (px) { texture_upload_2d(&t, TEXTURE_FORMAT_SRGB8, (uint32_t)w, (uint32_t)h, px, true);
        texture_set_sampler(&t, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_REPEAT, GL_REPEAT);
        stbi_image_free(px); }
    return t;
}

int main(int argc, char **argv) {
    const char *albedo_path = argc > 1 ? argv[1] : "assets/arch/proc/prefabs/bake/tint.png";
    const char *shot = argc > 2 ? argv[2] : "/tmp/p004_material.ppm";

    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window *win = SDL_CreateWindow("p004_material", 0, 0, WIN, WIN, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    SDL_GLContext gc = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, gc);
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) return 1;
    gl_loader_t loader = { sdl_get_proc, NULL };

    texture_t albedo = load_srgb(&loader, albedo_path);

    /* Shader honouring the material uniform contract (albedo * tint, gated). */
    const char *vs = "#version 330 core\nin vec2 in_pos; in vec2 in_uv; out vec2 v_uv;\n"
        "void main(){ v_uv=in_uv; gl_Position=vec4(in_pos,0,1); }\n";
    const char *fs = "#version 330 core\nin vec2 v_uv; out vec4 o;\n"
        "uniform sampler2D u_albedo_map; uniform int u_has_albedo; uniform vec3 u_tint;\n"
        "void main(){ vec3 base = u_has_albedo==1 ? texture(u_albedo_map,v_uv).rgb : vec3(1.0);\n"
        "  o=vec4(base*u_tint,1.0); }\n";
    shader_program_t prog; char log[512] = { 0 };
    if (shader_program_create(&prog, &loader, vs, fs, log, sizeof(log)) != SHADER_PROGRAM_OK) {
        fprintf(stderr, "shader: %s\n", log); return 1; }
    shader_uniform_cache_t cache; shader_uniform_cache_init(&cache, &prog);
    GLuint ph = (GLuint)shader_program_handle(&prog);
    GLint a_pos = glGetAttribLocation(ph, "in_pos");
    GLint a_uv = glGetAttribLocation(ph, "in_uv");

    render_material_t mat; material_init(&mat);
    mat.maps[MATERIAL_TEX_ALBEDO] = &albedo;
    mat.tint[0] = 1.0f; mat.tint[1] = 0.55f; mat.tint[2] = 0.35f; /* warm terracotta */

    float quad[] = { -0.9f,-0.9f,0,0,  0.9f,-0.9f,1,0,  0.9f,0.9f,1,1,
                     -0.9f,-0.9f,0,0,  0.9f,0.9f,1,1,  -0.9f,0.9f,0,1 };
    GLuint vao, vbo; glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray((GLuint)a_pos);
    glVertexAttribPointer((GLuint)a_pos, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray((GLuint)a_uv);
    glVertexAttribPointer((GLuint)a_uv, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));

    glViewport(0, 0, WIN, WIN);
    for (int frame = 0; frame < 3; ++frame) {
        glClearColor(0.08f, 0.08f, 0.1f, 1.0f); glClear(GL_COLOR_BUFFER_BIT);
        shader_program_bind(&prog);
        material_bind(&mat, 0u, &cache, &prog);
        glBindVertexArray(vao); glDrawArrays(GL_TRIANGLES, 0, 6);
        if (frame == 1) save_ppm(shot, WIN, WIN);
        SDL_GL_SwapWindow(win); SDL_Delay(60);
    }
    texture_destroy(&albedo); shader_program_destroy(&prog);
    SDL_GL_DeleteContext(gc); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}

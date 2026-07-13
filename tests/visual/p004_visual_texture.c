/**
 * @file p004_visual_texture.c
 * @brief Visual smoke test for texture_t: load a real bake PNG, upload it via
 *        the texture wrapper, sample it on a quad, and write a PPM screenshot.
 */
#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/texture.h"

#define WIN 512

static void *sdl_get_proc(const char *name, void *user) {
    (void)user;
    return SDL_GL_GetProcAddress(name);
}

static void save_ppm(const char *path, int w, int h) {
    size_t row = (size_t)w * 3;
    uint8_t *rgb = malloc(row * (size_t)h);
    if (!rgb) return;
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    FILE *f = fopen(path, "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int y = h - 1; y >= 0; --y) fwrite(rgb + (size_t)y * row, 1, row, f);
        fclose(f);
        printf("screenshot: %s\n", path);
    }
    free(rgb);
}

int main(int argc, char **argv) {
    const char *img_path = argc > 1 ? argv[1] : "assets/arch/proc/prefabs/bake/tint.png";
    const char *shot = argc > 2 ? argv[2] : "/tmp/p004_texture.ppm";

    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window *win = SDL_CreateWindow("p004_texture", 0, 0, WIN, WIN,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!win) { SDL_Quit(); return 1; }
    SDL_GLContext gc = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, gc);
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) { SDL_Quit(); return 1; }

    gl_loader_t loader = { sdl_get_proc, NULL };

    /* Load the PNG (force RGB) and upload it through texture_t. */
    int w = 0, h = 0, n = 0;
    unsigned char *pix = stbi_load(img_path, &w, &h, &n, 3);
    if (!pix) { fprintf(stderr, "stbi_load failed: %s\n", img_path); return 1; }
    texture_t tex;
    if (texture_create(&tex, &loader) != TEXTURE_OK) { fprintf(stderr, "tex create\n"); return 1; }
    texture_upload_2d(&tex, TEXTURE_FORMAT_SRGB8, (uint32_t)w, (uint32_t)h, pix, true);
    texture_set_sampler(&tex, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_REPEAT, GL_REPEAT);
    stbi_image_free(pix);
    printf("loaded %s (%dx%d) -> texture %u\n", img_path, w, h, texture_handle(&tex));

    const char *vs =
        "#version 330 core\n"
        "in vec2 in_pos; in vec2 in_uv; out vec2 v_uv;\n"
        "void main(){ v_uv=in_uv; gl_Position=vec4(in_pos,0.0,1.0); }\n";
    const char *fs =
        "#version 330 core\n"
        "in vec2 v_uv; uniform sampler2D u_tex; out vec4 f;\n"
        "void main(){ f=vec4(texture(u_tex,v_uv).rgb,1.0); }\n";
    shader_program_t prog;
    char log[512] = { 0 };
    if (shader_program_create(&prog, &loader, vs, fs, log, sizeof(log)) != SHADER_PROGRAM_OK) {
        fprintf(stderr, "shader: %s\n", log); return 1;
    }
    GLuint ph = (GLuint)shader_program_handle(&prog);
    GLint a_pos = glGetAttribLocation(ph, "in_pos");
    GLint a_uv = glGetAttribLocation(ph, "in_uv");
    GLint u_tex = glGetUniformLocation(ph, "u_tex");

    /* Quad covering most of the viewport: [pos.xy, uv.xy]. */
    float quad[] = {
        -0.9f, -0.9f, 0.0f, 0.0f,   0.9f, -0.9f, 1.0f, 0.0f,   0.9f, 0.9f, 1.0f, 1.0f,
        -0.9f, -0.9f, 0.0f, 0.0f,   0.9f,  0.9f, 1.0f, 1.0f,  -0.9f, 0.9f, 0.0f, 1.0f,
    };
    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray((GLuint)a_pos);
    glVertexAttribPointer((GLuint)a_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray((GLuint)a_uv);
    glVertexAttribPointer((GLuint)a_uv, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));

    glViewport(0, 0, WIN, WIN);
    int saved = 0;
    for (int frame = 0; frame < 3; ++frame) {
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        shader_program_bind(&prog);
        texture_bind(&tex, 0);
        glUniform1i(u_tex, 0);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        if (frame == 1 && !saved) { save_ppm(shot, WIN, WIN); saved = 1; }
        SDL_GL_SwapWindow(win);
        SDL_Delay(60);
    }

    texture_destroy(&tex);
    shader_program_destroy(&prog);
    SDL_GL_DeleteContext(gc);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

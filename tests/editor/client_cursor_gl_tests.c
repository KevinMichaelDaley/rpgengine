/**
 * @file client_cursor_gl_tests.c
 * @brief GL integration tests for 3D editor cursor rendering.
 *
 * Creates an offscreen GL context via SDL2, renders cursor debug lines
 * to an FBO, reads back pixels and verifies non-black content, then
 * saves a PPM screenshot to build/cursor_screenshot.ppm for inspection.
 *
 * Compile:
 *   gcc -std=c11 -Wall -Wextra -Wpedantic -Iinclude \
 *       tests/editor/client_cursor_gl_tests.c \
 *       src/editor/client/client_cursor.c \
 *       src/editor/client/client_cursor_render.c \
 *       src/renderer/debug_lines/debug_lines.c \
 *       build/glad.o \
 *       -o build/client_cursor_gl_tests \
 *       -lm -lSDL2 -ldl
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glad/glad.h>
#include <SDL2/SDL.h>

#include "ferrum/editor/client/client_cursor.h"
#include "ferrum/renderer/debug_lines.h"
#include "ferrum/math/vec3.h"

/* ----------------------------------------------------------------------- */
/* Test macros                                                               */
/* ----------------------------------------------------------------------- */

static int g_pass, g_fail;

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); \
    if (fn()) { printf("OK   %s\n", #fn); g_pass++; } \
    else       { printf("FAIL %s\n", #fn); g_fail++; } \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  ASSERT FAILED: %s (line %d)\n", #cond, __LINE__); \
        return false; \
    } \
} while (0)

/* ----------------------------------------------------------------------- */
/* GL context setup                                                          */
/* ----------------------------------------------------------------------- */

static SDL_Window   *g_window;
static SDL_GLContext  g_gl;

static const int FB_W = 256;
static const int FB_H = 256;

static bool gl_setup(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    g_window = SDL_CreateWindow("cursor_gl_test",
                                SDL_WINDOWPOS_UNDEFINED,
                                SDL_WINDOWPOS_UNDEFINED,
                                FB_W, FB_H,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!g_window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    g_gl = SDL_GL_CreateContext(g_window);
    if (!g_gl) {
        fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return false;
    }

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "gladLoadGLLoader failed\n");
        SDL_GL_DeleteContext(g_gl);
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return false;
    }

    printf("  GL: %s / %s\n", glGetString(GL_RENDERER),
           glGetString(GL_VERSION));
    return true;
}

static void gl_teardown(void) {
    if (g_gl) SDL_GL_DeleteContext(g_gl);
    if (g_window) SDL_DestroyWindow(g_window);
    SDL_Quit();
}

/* ----------------------------------------------------------------------- */
/* Minimal shader for colored lines                                          */
/* ----------------------------------------------------------------------- */

static GLuint compile_shader_(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "shader error: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint create_line_program_(void) {
    const char *vs =
        "#version 330 core\n"
        "layout(location=0) in vec3 in_pos;\n"
        "uniform mat4 u_mvp;\n"
        "void main() { gl_Position = u_mvp * vec4(in_pos, 1.0); }\n";

    const char *fs =
        "#version 330 core\n"
        "uniform vec3 u_color;\n"
        "out vec4 frag;\n"
        "void main() { frag = vec4(u_color, 1.0); }\n";

    GLuint v = compile_shader_(GL_VERTEX_SHADER, vs);
    if (!v) return 0;
    GLuint f = compile_shader_(GL_FRAGMENT_SHADER, fs);
    if (!f) { glDeleteShader(v); return 0; }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, v);
    glAttachShader(prog, f);
    glLinkProgram(prog);
    glDeleteShader(v);
    glDeleteShader(f);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "link error: %s\n", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

/* ----------------------------------------------------------------------- */
/* Simple orthographic MVP (identity-like, maps [-5,5] to clip)              */
/* ----------------------------------------------------------------------- */

static void ortho_mvp_(float *m, float l, float r, float b, float t,
                       float n, float f) {
    memset(m, 0, 16 * sizeof(float));
    m[0]  = 2.0f / (r - l);
    m[5]  = 2.0f / (t - b);
    m[10] = -2.0f / (f - n);
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[14] = -(f + n) / (f - n);
    m[15] = 1.0f;
}

/* ----------------------------------------------------------------------- */
/* PPM screenshot                                                            */
/* ----------------------------------------------------------------------- */

static bool save_ppm_(const char *path, const unsigned char *rgb,
                      int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    /* GL reads bottom-up; flip rows. */
    for (int y = h - 1; y >= 0; y--) {
        fwrite(rgb + y * w * 3, 1, (size_t)(w * 3), f);
    }
    fclose(f);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Tests                                                                     */
/* ----------------------------------------------------------------------- */

/**
 * Render cursor lines to an FBO, verify some pixels are non-black,
 * save a screenshot.
 */
static bool test_render_cursor_to_fbo(void) {
    /* Create FBO with color + depth. */
    GLuint fbo = 0, color_tex = 0, depth_rb = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &color_tex);
    glBindTexture(GL_TEXTURE_2D, color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, FB_W, FB_H, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, color_tex, 0);

    glGenRenderbuffers(1, &depth_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, FB_W, FB_H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, depth_rb);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    ASSERT(status == GL_FRAMEBUFFER_COMPLETE);

    /* Create shader + VAO/VBO. */
    GLuint prog = create_line_program_();
    ASSERT(prog != 0);

    GLint u_mvp = glGetUniformLocation(prog, "u_mvp");
    GLint u_color = glGetUniformLocation(prog, "u_color");

    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    /* Set up cursor at origin. */
    editor_cursor_t cur;
    editor_cursor_init(&cur);
    cur.grid_size = 1.0f;

    /* Submit cursor lines into the debug line store. */
    fr_debug_line_t storage[64];
    fr_debug_lines_t lines;
    fr_debug_lines_init(&lines, storage, 64);
    uint32_t n_lines = editor_cursor_submit_lines(&cur, &lines, 0.0, 1.0);
    ASSERT(n_lines == 7);

    /* Collect vertices. */
    vec3_t verts[128];
    size_t vert_count = 0;
    bool ok = fr_debug_lines_collect_vertices(&lines, 0.001, verts, 128,
                                               &vert_count);
    ASSERT(ok);
    ASSERT(vert_count == 14);

    /* Upload vertices. */
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(vert_count * sizeof(vec3_t)),
                 verts, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vec3_t), NULL);
    glEnableVertexAttribArray(0);

    /* Render. */
    glViewport(0, 0, FB_W, FB_H);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(prog);

    /* Wider lines for visibility on software rasterizers. */
    glLineWidth(3.0f);

    /* Orthographic: camera looking down -Z, covering [-3,3] in XY. */
    float mvp[16];
    ortho_mvp_(mvp, -3.0f, 3.0f, -3.0f, 3.0f, -10.0f, 10.0f);
    glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp);

    /* Draw grid first (under axes), then each axis on top. */
    /* Grid = lines 3..6 (verts 6..13) — yellow */
    glUniform3f(u_color, 1.0f, 1.0f, 0.0f);
    glDrawArrays(GL_LINES, 6, 8);

    /* X axis = lines 0 (verts 0..1) — red */
    glUniform3f(u_color, 1.0f, 0.0f, 0.0f);
    glDrawArrays(GL_LINES, 0, 2);

    /* Y axis = lines 1 (verts 2..3) — green */
    glUniform3f(u_color, 0.0f, 1.0f, 0.0f);
    glDrawArrays(GL_LINES, 2, 2);

    /* Z axis = lines 2 (verts 4..5) — blue (into screen, may not be visible) */
    glUniform3f(u_color, 0.0f, 0.0f, 1.0f);
    glDrawArrays(GL_LINES, 4, 2);

    glFinish();

    /* Read pixels. */
    unsigned char *pixels = malloc((size_t)(FB_W * FB_H * 3));
    ASSERT(pixels != NULL);
    glReadPixels(0, 0, FB_W, FB_H, GL_RGB, GL_UNSIGNED_BYTE, pixels);

    /* Verify: at least some pixels are non-black. */
    int non_black = 0;
    for (int i = 0; i < FB_W * FB_H * 3; i++) {
        if (pixels[i] > 0) non_black++;
    }
    printf("  non-black pixel components: %d / %d\n",
           non_black, FB_W * FB_H * 3);
    ASSERT(non_black > 0);

    /* Save screenshot first for debugging. */
    bool saved = save_ppm_("build/cursor_screenshot.ppm", pixels, FB_W, FB_H);
    printf("  screenshot saved: %s\n", saved ? "yes" : "no");

    /* Check for red-dominant pixels (X axis).
     * Relaxed threshold for software rasterizers (e.g., llvmpipe). */
    int red_count = 0;
    for (int i = 0; i < FB_W * FB_H; i++) {
        unsigned char r = pixels[i * 3];
        unsigned char g = pixels[i * 3 + 1];
        unsigned char b = pixels[i * 3 + 2];
        if (r > 100 && r > g + 30 && r > b + 30) {
            red_count++;
        }
    }
    printf("  red-dominant pixels (X axis): %d\n", red_count);
    ASSERT(red_count > 0);

    /* Check for green-dominant pixels (Y axis). */
    int green_count = 0;
    for (int i = 0; i < FB_W * FB_H; i++) {
        unsigned char r = pixels[i * 3];
        unsigned char g = pixels[i * 3 + 1];
        unsigned char b = pixels[i * 3 + 2];
        if (g > 100 && g > r + 30 && g > b + 30) {
            green_count++;
        }
    }
    printf("  green-dominant pixels (Y axis): %d\n", green_count);
    ASSERT(green_count > 0);

    /* Check for yellow-ish pixels (grid). Grid lines on XZ plane are
     * visible since our ortho camera looks down -Z and the grid is
     * at Y=0; the edges project as horizontal/vertical lines. */
    int yellow_count = 0;
    for (int i = 0; i < FB_W * FB_H; i++) {
        unsigned char r = pixels[i * 3];
        unsigned char g = pixels[i * 3 + 1];
        unsigned char b = pixels[i * 3 + 2];
        if (r > 100 && g > 100 && b < r / 2) {
            yellow_count++;
        }
    }
    printf("  yellow pixels (grid): %d\n", yellow_count);

    free(pixels);

    /* Cleanup. */
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(prog);
    glDeleteTextures(1, &color_tex);
    glDeleteRenderbuffers(1, &depth_rb);
    glDeleteFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return true;
}

/** Cursor at off-center position still produces visible lines. */
static bool test_render_cursor_offset(void) {
    GLuint fbo = 0, color_tex = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &color_tex);
    glBindTexture(GL_TEXTURE_2D, color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, FB_W, FB_H, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, color_tex, 0);
    ASSERT(glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
           GL_FRAMEBUFFER_COMPLETE);

    GLuint prog = create_line_program_();
    ASSERT(prog != 0);
    GLint u_mvp = glGetUniformLocation(prog, "u_mvp");
    GLint u_color = glGetUniformLocation(prog, "u_color");

    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    /* Cursor offset from origin. */
    editor_cursor_t cur;
    editor_cursor_init(&cur);
    cur.position = (vec3_t){1.5f, 0.5f, 0.0f};
    cur.grid_size = 0.5f;

    fr_debug_line_t storage[64];
    fr_debug_lines_t lines;
    fr_debug_lines_init(&lines, storage, 64);
    editor_cursor_submit_lines(&cur, &lines, 0.0, 1.0);

    vec3_t verts[128];
    size_t vert_count = 0;
    fr_debug_lines_collect_vertices(&lines, 0.001, verts, 128, &vert_count);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(vert_count * sizeof(vec3_t)),
                 verts, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vec3_t), NULL);
    glEnableVertexAttribArray(0);

    glViewport(0, 0, FB_W, FB_H);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(prog);
    float mvp[16];
    ortho_mvp_(mvp, -3.0f, 3.0f, -3.0f, 3.0f, -10.0f, 10.0f);
    glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp);

    glUniform3f(u_color, 1.0f, 1.0f, 1.0f);
    glDrawArrays(GL_LINES, 0, (GLsizei)vert_count);
    glFinish();

    unsigned char *pixels = malloc((size_t)(FB_W * FB_H * 3));
    ASSERT(pixels != NULL);
    glReadPixels(0, 0, FB_W, FB_H, GL_RGB, GL_UNSIGNED_BYTE, pixels);

    int non_black = 0;
    for (int i = 0; i < FB_W * FB_H * 3; i++) {
        if (pixels[i] > 0) non_black++;
    }
    printf("  offset cursor non-black: %d\n", non_black);
    ASSERT(non_black > 0);

    save_ppm_("build/cursor_offset_screenshot.ppm", pixels, FB_W, FB_H);
    free(pixels);

    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(prog);
    glDeleteTextures(1, &color_tex);
    glDeleteFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return true;
}

/** Hidden cursor produces empty (all-black) framebuffer. */
static bool test_render_cursor_hidden(void) {
    GLuint fbo = 0, color_tex = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &color_tex);
    glBindTexture(GL_TEXTURE_2D, color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, FB_W, FB_H, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, color_tex, 0);

    glViewport(0, 0, FB_W, FB_H);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    editor_cursor_t cur;
    editor_cursor_init(&cur);
    cur.visible = false;

    fr_debug_line_t storage[64];
    fr_debug_lines_t lines;
    fr_debug_lines_init(&lines, storage, 64);
    uint32_t n = editor_cursor_submit_lines(&cur, &lines, 0.0, 1.0);
    ASSERT(n == 0);

    /* Nothing drawn → all black. */
    unsigned char *pixels = malloc((size_t)(FB_W * FB_H * 3));
    ASSERT(pixels != NULL);
    glReadPixels(0, 0, FB_W, FB_H, GL_RGB, GL_UNSIGNED_BYTE, pixels);

    int non_black = 0;
    for (int i = 0; i < FB_W * FB_H * 3; i++) {
        if (pixels[i] > 0) non_black++;
    }
    ASSERT(non_black == 0);

    free(pixels);
    glDeleteTextures(1, &color_tex);
    glDeleteFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return true;
}

/* ----------------------------------------------------------------------- */
/* Main                                                                      */
/* ----------------------------------------------------------------------- */

int main(void) {
    if (!gl_setup()) {
        printf("SKIP: could not create GL context\n");
        return 0;
    }

    RUN(test_render_cursor_to_fbo);
    RUN(test_render_cursor_offset);
    RUN(test_render_cursor_hidden);

    gl_teardown();

    printf("\n%d / %d GL tests passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}

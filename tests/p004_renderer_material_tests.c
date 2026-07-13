/**
 * @file p004_renderer_material_tests.c
 * @brief Unit tests for render_material_t against a real hidden GL context.
 */
#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/material.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"
#include "ferrum/renderer/texture.h"

#define ASSERT_TRUE(cond)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "ASSERT failed %s:%d: %s\n", __FILE__, __LINE__,  \
                    #cond);                                                   \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static gl_loader_t g_loader;
static SDL_Window *g_win;
static SDL_GLContext g_ctx;

static void *sdl_get_proc(const char *name, void *user) { (void)user; return SDL_GL_GetProcAddress(name); }

static int ctx_init(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return -1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    g_win = SDL_CreateWindow("mat_tests", 0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!g_win) { SDL_Quit(); return -1; }
    g_ctx = SDL_GL_CreateContext(g_win);
    if (!g_ctx) { SDL_DestroyWindow(g_win); SDL_Quit(); return -1; }
    SDL_GL_MakeCurrent(g_win, g_ctx);
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) { SDL_Quit(); return -1; }
    (void)glGetError();
    g_loader.get_proc_address = sdl_get_proc; g_loader.user_data = NULL;
    return 0;
}

/* A shader that references the material contract uniforms so they stay active. */
static const char *VS = "#version 330 core\nin vec2 p;\nvoid main(){gl_Position=vec4(p,0,1);}\n";
static const char *FS =
    "#version 330 core\nout vec4 o;\n"
    "uniform sampler2D u_albedo_map; uniform sampler2D u_lightmap;\n"
    "uniform int u_has_albedo; uniform int u_has_lightmap;\n"
    "uniform vec3 u_tint; uniform float u_roughness_min; uniform float u_metalness;\n"
    "void main(){ vec3 c=u_tint*float(u_has_albedo+u_has_lightmap);\n"
    "  c+=texture(u_albedo_map,vec2(0.5)).rgb + texture(u_lightmap,vec2(0.5)).rgb;\n"
    "  o=vec4(c*(u_roughness_min+u_metalness+1.0),1.0); }\n";

static int test_defaults(void) {
    render_material_t m;
    material_init(&m);
    ASSERT_TRUE(m.tint[0] == 1.0f && m.tint[1] == 1.0f && m.tint[2] == 1.0f);
    ASSERT_TRUE(m.specular_strength == 0.5f && m.metalness == 0.0f);
    ASSERT_TRUE(m.roughness_min == 0.0f && m.roughness_max == 1.0f);
    for (int i = 0; i < MATERIAL_TEX_COUNT; ++i) ASSERT_TRUE(m.maps[i] == NULL);
    material_init(NULL); /* NULL-safe */
    return 0;
}

static int test_bind_counts_and_clean(void) {
    shader_program_t prog;
    char log[512] = { 0 };
    ASSERT_TRUE(shader_program_create(&prog, &g_loader, VS, FS, log, sizeof(log)) == SHADER_PROGRAM_OK);
    shader_uniform_cache_t cache;
    shader_uniform_cache_init(&cache, &prog);
    shader_program_bind(&prog);

    texture_t albedo, lm;
    ASSERT_TRUE(texture_create(&albedo, &g_loader) == TEXTURE_OK);
    ASSERT_TRUE(texture_create(&lm, &g_loader) == TEXTURE_OK);
    uint8_t px[4] = { 200, 150, 100, 255 };
    float lf[3] = { 1.0f, 1.0f, 1.0f };
    texture_upload_2d(&albedo, TEXTURE_FORMAT_SRGB8_A8, 1, 1, px, false);
    texture_upload_2d(&lm, TEXTURE_FORMAT_RGB32F, 1, 1, lf, false);

    render_material_t m;
    material_init(&m);
    m.maps[MATERIAL_TEX_ALBEDO] = &albedo;
    m.maps[MATERIAL_TEX_LIGHTMAP] = &lm;
    m.tint[0] = 0.9f; m.tint[1] = 0.4f; m.tint[2] = 0.2f;

    uint32_t bound = material_bind(&m, 0u, &cache, &prog);
    ASSERT_TRUE(bound == 2u);
    ASSERT_TRUE(glGetError() == GL_NO_ERROR);

    /* NULL args are rejected. */
    ASSERT_TRUE(material_bind(NULL, 0u, &cache, &prog) == 0u);
    ASSERT_TRUE(material_bind(&m, 0u, NULL, &prog) == 0u);

    texture_destroy(&albedo); texture_destroy(&lm);
    shader_program_destroy(&prog);
    return 0;
}

struct tc { const char *name; int (*fn)(void); };
static struct tc TESTS[] = {
    { "defaults", test_defaults },
    { "bind_counts_and_clean", test_bind_counts_and_clean },
};

int main(void) {
    if (ctx_init() != 0) { fprintf(stderr, "no GL context; skipping\n"); return 0; }
    int failed = 0;
    for (size_t i = 0; i < sizeof(TESTS) / sizeof(TESTS[0]); ++i) {
        printf("RUN  %s\n", TESTS[i].name);
        int r = TESTS[i].fn();
        printf(r == 0 ? "OK   %s\n" : "FAIL %s\n", TESTS[i].name);
        failed += (r != 0);
    }
    if (g_ctx) SDL_GL_DeleteContext(g_ctx);
    if (g_win) SDL_DestroyWindow(g_win);
    SDL_Quit();
    printf("%s (%d failed)\n", failed ? "FAILED" : "PASSED", failed);
    return failed ? 1 : 0;
}

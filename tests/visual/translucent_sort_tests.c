/**
 * @file translucent_sort_tests.c
 * @brief TDD tests for the sorted translucent pass in forward+ (rpg-rxf8):
 *        renderables with material opacity < 1 leave the opaque loop (and the
 *        depth pre-pass) and draw in a 4th graph node after 'forward', sorted
 *        back-to-front by view-space AABB-centre depth, GL_BLEND
 *        (SRC_ALPHA, ONE_MINUS_SRC_ALPHA), depth test LEQUAL, depth WRITE off,
 *        full clustered lighting + u_opacity alpha.
 *
 * Scene: white ground at y=0, a RED glass quad at y=2 and a BLUE glass quad
 * at y=4 sharing a footprint, camera straight overhead. Correct back-to-front
 * blending composites blue LAST:
 *   C = 0.5*blue + 0.25*red + 0.25*ground   (blue-dominant)
 * while unsorted submission order would flip the weights (red-dominant).
 *   happy: centre pixel is blue-dominant;
 *   happy: swapping the SCENE submission order yields the same pixel (the
 *          pass sorts, so submission order is irrelevant);
 *   edge:  ground away from the quads is unaffected (opaque path regression);
 *   edge:  ground THROUGH the glass is brighter than black (blend, not
 *          replace) and darker than open ground (glass does attenuate).
 */
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>
#include "glad/glad.h"

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/material.h"
#include "ferrum/renderer/mesh/static_mesh.h"
#include "ferrum/renderer/render_camera.h"
#include "ferrum/renderer/render_forward.h"
#include "ferrum/renderer/render_scene.h"
#include "ferrum/renderer/light_store.h"

#define WIN 256

#define ASSERT_TRUE(expr)                                                     \
    do { if (!(expr)) { fprintf(stderr, "  ASSERT_TRUE failed: %s (%s:%d)\n", \
        #expr, __FILE__, __LINE__); return 1; } } while (0)

static void *sdl_get_proc(const char *n, void *u) { (void)u; return SDL_GL_GetProcAddress(n); }

static int make_quad_xz(const gl_loader_t *loader, static_mesh_t *out,
                        render_submesh_t *sub,
                        float x0, float z0, float x1, float z1, float y)
{
    float pos[12] = { x0, y, z0, x1, y, z0, x1, y, z1, x0, y, z1 };
    float nrm[12] = { 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0 };
    float tan[16] = { 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1 };
    float uv[8]   = { 0, 0, 1, 0, 1, 1, 0, 1 };
    uint32_t idx[6] = { 0, 2, 1, 0, 3, 2 };
    *sub = (render_submesh_t){ 0, 6, 0 };
    static_mesh_create_info_t info;
    memset(&info, 0, sizeof info);
    info.positions = pos; info.normals = nrm; info.tangents = tan;
    info.uv0 = uv; info.uv1 = uv;
    info.indices = idx; info.vertex_count = 4; info.index_count = 6;
    info.submeshes = sub; info.submesh_count = 1;
    return static_mesh_create(loader, &info, out) == 0;
}

/* 5x5 average around a screen point, [0,1] rgb. */
static void sample_px(const uint8_t *px, int sx, int sy, float rgb[3])
{
    int n = 0; float acc[3] = { 0, 0, 0 };
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            int x = sx + dx, y = sy + dy;
            if (x < 0 || y < 0 || x >= WIN || y >= WIN) continue;
            const uint8_t *p = px + (size_t)(y * WIN + x) * 3u;
            acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2]; ++n;
        }
    for (int c = 0; c < 3; ++c) rgb[c] = n ? acc[c] / (255.0f * (float)n) : 0.0f;
}

/* Render with the two glass quads submitted in the given order (0 = red
 * first, 1 = blue first). Returns the centre pixel (both quads overlap),
 * and the open-ground pixel. */
static int render_case(const gl_loader_t *loader, int blue_first,
                       float center[3], float open_rgb[3])
{
    static_mesh_t ground, red, blue;
    render_submesh_t s0, s1, s2;
    ASSERT_TRUE(make_quad_xz(loader, &ground, &s0, -12, -12, 12, 12, 0.0f));
    ASSERT_TRUE(make_quad_xz(loader, &red, &s1, -3, -3, 3, 3, 2.0f));
    ASSERT_TRUE(make_quad_xz(loader, &blue, &s2, -3, -3, 3, 3, 4.0f));

    render_material_t m_ground, m_red, m_blue;
    material_init(&m_ground);
    material_init(&m_red);
    m_red.tint[0] = 0.9f; m_red.tint[1] = 0.1f; m_red.tint[2] = 0.1f;
    m_red.opacity = 0.5f;
    material_init(&m_blue);
    m_blue.tint[0] = 0.1f; m_blue.tint[1] = 0.1f; m_blue.tint[2] = 0.9f;
    m_blue.opacity = 0.5f;

    render_renderable_t rbacking[4];
    render_scene_t scene;
    render_scene_init(&scene, rbacking, 4);
    float model[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    render_scene_add(&scene, &ground, &m_ground, model);
    if (blue_first) {
        render_scene_add(&scene, &blue, &m_blue, model);
        render_scene_add(&scene, &red, &m_red, model);
    } else {
        render_scene_add(&scene, &red, &m_red, model);
        render_scene_add(&scene, &blue, &m_blue, model);
    }

    render_light_t lb[4];
    render_light_store_t lights;
    render_light_store_init(&lights, lb, 4);
    scene.lights = &lights;

    const float ch = 14.0f, fov = 45.0f * (float)M_PI / 180.0f;
    float eye[3] = { 0.0f, ch, 0.0f }, tgt[3] = { 0.0f, 0.0f, 0.0f };
    float up[3] = { 0, 0, 1 };
    render_camera_look_at(&scene.camera, eye, tgt, up, fov, 1.0f, 0.2f, 60.0f);

    render_forward_config_t fcfg;
    memset(&fcfg, 0, sizeof fcfg);
    fcfg.loader = loader;
    fcfg.cluster = (cluster_config_t){ 8, 8, 8, 0.2f, 60.0f };
    fcfg.max_lights = 4;
    fcfg.index_capacity = 8u * 8u * 8u * 4u;
    fcfg.screen_w = (float)WIN; fcfg.screen_h = (float)WIN;
    fcfg.sun_dir[0] = 0.0f; fcfg.sun_dir[1] = 1.0f; fcfg.sun_dir[2] = 0.0f;
    fcfg.sun_color[0] = 1.6f; fcfg.sun_color[1] = 1.6f; fcfg.sun_color[2] = 1.6f;
    fcfg.ambient[0] = fcfg.ambient[1] = fcfg.ambient[2] = 0.02f;
    fcfg.shadow_light = -1;
    fcfg.spot_light = -1;    /* no shadows: isolate the blend behaviour. */

    render_forward_t fwd;
    ASSERT_TRUE(render_forward_init(&fwd, &fcfg));

    /* Offscreen target (hidden-window backbuffers are undefined). */
    GLuint fbo = 0, col = 0, dep = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &col);
    glBindTexture(GL_TEXTURE_2D, col);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, WIN, WIN, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, col, 0);
    glGenRenderbuffers(1, &dep);
    glBindRenderbuffer(GL_RENDERBUFFER, dep);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, WIN, WIN);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, dep);
    ASSERT_TRUE(glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
                GL_FRAMEBUFFER_COMPLETE);
    fwd.target_fbo = fbo;   /* explicit offscreen target for the main graph. */

    glViewport(0, 0, WIN, WIN);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    render_forward_render(&fwd, &scene);

    static uint8_t px[WIN * WIN * 3];
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glReadPixels(0, 0, WIN, WIN, GL_RGB, GL_UNSIGNED_BYTE, px);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    /* Centre of the stack; open ground near the edge (world x ~ +4.6). */
    sample_px(px, WIN / 2, WIN / 2, center);
    sample_px(px, WIN / 2 - 100, WIN / 2, open_rgb);

    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &col);
    glDeleteRenderbuffers(1, &dep);
    render_forward_destroy(&fwd);
    static_mesh_destroy(&ground);
    static_mesh_destroy(&red);
    static_mesh_destroy(&blue);
    return 0;
}

int main(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 77;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window *win = SDL_CreateWindow("translucent sort", 0, 0, WIN, WIN,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!win) return 77;
    SDL_GLContext gc = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, gc);
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) return 77;
    gl_loader_t loader = { sdl_get_proc, NULL };

    int fails = 0;
    float c_a[3], o_a[3], c_b[3], o_b[3];
    if (render_case(&loader, 0, c_a, o_a)) fails |= 1;
    if (render_case(&loader, 1, c_b, o_b)) fails |= 2;
    if (!fails) {
        fprintf(stderr, "order red-first : center=(%.3f %.3f %.3f) open=%.3f\n",
                c_a[0], c_a[1], c_a[2], o_a[1]);
        fprintf(stderr, "order blue-first: center=(%.3f %.3f %.3f)\n",
                c_b[0], c_b[1], c_b[2]);
        /* Display-space blend expectation (the shader tonemaps before the
         * blend): C = 0.5*blue_px + 0.25*red_px + 0.25*ground_px, with
         * blue_px ~ (.26,.26,.59), red_px ~ (.59,.26,.26), ground ~ .61 =>
         * C ~ (.43,.35,.51). Opaque-replace would read the bare blue quad
         * (.26,.26,.59) instead -- each assert below rejects that. */
        /* happy: the RED quad shows THROUGH the blue one (blend, not
         * depth-replace: bare blue reads r=.26). */
        if (!(c_a[0] > 0.33f)) {
            fprintf(stderr, "  FAIL: back quad not visible through front\n");
            fails |= 4;
        }
        /* happy: correct order keeps the FRONT (blue) quad dominant; the
         * wrong order composites red last => r >= b. */
        if (!(c_a[2] > c_a[0] + 0.04f)) {
            fprintf(stderr, "  FAIL: not blue-dominant (wrong blend order?)\n");
            fails |= 8;
        }
        /* happy: the pass SORTS -- submission order must not matter. */
        for (int ch = 0; ch < 3; ++ch)
            if (fabsf(c_a[ch] - c_b[ch]) > 0.02f) {
                fprintf(stderr, "  FAIL: submission order changed the pixel\n");
                fails |= 16;
                break;
            }
        /* edge: open ground unaffected and clearly lit. */
        if (!(o_a[1] > 0.3f)) { fprintf(stderr, "  FAIL: open ground dark\n"); fails |= 32; }
        /* edge: the white ground leaks through the stack (green channel only
         * comes from the ground; bare glass reads g=.26) yet stays darker
         * than open ground. */
        if (!(c_a[1] > 0.30f && c_a[1] < o_a[1] - 0.05f)) {
            fprintf(stderr, "  FAIL: stack not blended with ground\n");
            fails |= 64;
        }
    }
    fprintf(stderr, "translucent_sort_tests: %s (0x%x)\n",
            fails ? "FAIL" : "all ok", fails);
    SDL_GL_DeleteContext(gc);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return fails;
}

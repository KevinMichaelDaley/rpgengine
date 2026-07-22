/**
 * @file translucent_shadow_tests.c
 * @brief TDD tests for the CSM translucency mask (rpg-29zj): translucent
 *        casters leave the MAIN shadow map (light passes through) and render
 *        into a depth-enabled mask (tint + coverage + distance) instead; the
 *        receiver multiplies the sun term by the mask tint when it sits
 *        BEYOND the mask depth (behind the glass as seen from the light).
 *
 * Written before the implementation (TDD phase 1) -- drives the API:
 *   render_forward_config_t.dir_translucency  (enable the mask targets)
 *   shadow_csm_mask_* (init / bake / bind, exercised via render_forward)
 *
 * Scene: a big ground plane (opaque), a BLUE glass panel (opacity 0.30)
 * floating above one area, an OPAQUE panel above another. A tilted sun
 * displaces both shadows sideways so the camera (straight overhead) sees the
 * shadowed ground beside each panel. Numeric framebuffer asserts:
 *   happy:   ground behind glass is LIT but attenuated and blue-shifted;
 *   happy:   ground behind the opaque panel is plain dark (regression);
 *   edge:    ground in the open reads full sun (mask does not darken it);
 *   edge:    disabling dir_translucency reverts glass to a HARD shadow
 *            (translucent casters fall back into the main map).
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

#define WIN 512

#define ASSERT_TRUE(expr)                                                     \
    do { if (!(expr)) { fprintf(stderr, "  ASSERT_TRUE failed: %s (%s:%d)\n", \
        #expr, __FILE__, __LINE__); return 1; } } while (0)

static void *sdl_get_proc(const char *n, void *u) { (void)u; return SDL_GL_GetProcAddress(n); }

/* KHR_debug sink (TSHADOW_GLDEBUG): print driver messages for errors. */
void tshadow_debug_cb(GLenum src, GLenum type, GLuint id, GLenum sev,
                      GLsizei len, const GLchar *msg, const void *user)
{
    (void)src; (void)id; (void)len; (void)user;
    if (type == 0x824C /* DEBUG_TYPE_ERROR */ || sev == 0x9146 /* HIGH */)
        fprintf(stderr, "  [GL] %s\n", msg);
}

/* One axis-aligned quad in the XZ plane at height y (two triangles, +Y normal). */
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

/* Average a 5x5 framebuffer patch around the projection of world point (wx,wz)
 * on the ground plane, for the straight-down camera at (cx, ch, cz), up=+Z:
 * screen right = -X, screen up = +Z. */
static void sample_ground(const uint8_t *px, float cx, float cz, float ch,
                          float fov_rad, float wx, float wz, float rgb[3])
{
    float half = ch * tanf(fov_rad * 0.5f);
    int sx = (int)((1.0f - (wx - cx) / half) * 0.5f * WIN);
    int sy = (int)((1.0f + (wz - cz) / half) * 0.5f * WIN);
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

static float lum(const float rgb[3]) { return 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2]; }

/* Render the scene once with/without the translucency mask (and optionally
 * the caustics compute refining it) and return the three ground patches:
 * open sun / behind glass / behind opaque panel. */
static int render_case(const gl_loader_t *loader, int translucency,
                       int caustics, float open_rgb[3], float glass_rgb[3],
                       float opaque_rgb[3])
{
    static_mesh_t ground, glass, panel;
    render_submesh_t s0, s1, s2;
    ASSERT_TRUE(make_quad_xz(loader, &ground, &s0, -12, -12, 12, 12, 0.0f));
    ASSERT_TRUE(make_quad_xz(loader, &glass, &s1, -4, -1.5f, -1, 1.5f, 2.0f));
    ASSERT_TRUE(make_quad_xz(loader, &panel, &s2, 1.5f, -1.5f, 3.5f, 1.5f, 2.0f));

    render_material_t m_ground, m_glass, m_panel;
    material_init(&m_ground);
    m_ground.roughness_min = 0.8f; m_ground.roughness_max = 0.9f;
    material_init(&m_glass);
    /* Deeply saturated blue: the Reinhard+gamma output curve compresses colour
     * ratios, so the transmitted blue-shift must start strong to stay visible. */
    m_glass.tint[0] = 0.08f; m_glass.tint[1] = 0.30f; m_glass.tint[2] = 0.95f;
    m_glass.opacity = 0.30f;
    material_init(&m_panel);
    m_panel.roughness_min = 0.8f; m_panel.roughness_max = 0.9f;

    render_renderable_t rbacking[4];
    render_scene_t scene;
    render_scene_init(&scene, rbacking, 4);
    float model[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    render_scene_add(&scene, &ground, &m_ground, model);
    render_scene_add(&scene, &glass, &m_glass, model);
    render_scene_add(&scene, &panel, &m_panel, model);

    render_light_t lb[4];
    render_light_store_t lights;
    render_light_store_init(&lights, lb, 4);
    scene.lights = &lights;

    const float ch = 14.0f, fov = 45.0f * (float)M_PI / 180.0f;
    float eye[3] = { 0.5f, ch, 0.0f }, tgt[3] = { 0.5f, 0.0f, 0.0f };
    float up[3] = { 0, 0, 1 };
    render_camera_look_at(&scene.camera, eye, tgt, up, fov, 1.0f, 0.2f, 60.0f);

    render_forward_config_t fcfg;
    memset(&fcfg, 0, sizeof fcfg);
    fcfg.loader = loader;
    fcfg.cluster = (cluster_config_t){ 8, 8, 8, 0.2f, 60.0f };
    fcfg.max_lights = 4;
    fcfg.index_capacity = 8u * 8u * 8u * 4u;
    fcfg.screen_w = (float)WIN; fcfg.screen_h = (float)WIN;
    /* u_sun_dir points TOWARD the sun, so light travels along (0.5, -1, 0):
     * shadows displace +X and the overhead camera (half-width 5.8 around
     * x = 0.5) sees the shadowed ground beside each floating panel. */
    fcfg.sun_dir[0] = -0.5f; fcfg.sun_dir[1] = 1.0f; fcfg.sun_dir[2] = 0.0f;
    fcfg.sun_color[0] = 1.6f; fcfg.sun_color[1] = 1.6f; fcfg.sun_color[2] = 1.6f;
    fcfg.ambient[0] = fcfg.ambient[1] = fcfg.ambient[2] = 0.02f;
    fcfg.shadow_light = -1;
    fcfg.spot_light = -1;
    fcfg.dir_cascades = 2;
    fcfg.dir_static_res = 1024;
    fcfg.dir_dynamic_res = 256;
    fcfg.dir_lambda = 0.6f;
    fcfg.dir_bias = 0.05f;
    fcfg.shadow_scene_min[0] = -12; fcfg.shadow_scene_min[1] = -1; fcfg.shadow_scene_min[2] = -12;
    fcfg.shadow_scene_max[0] = 12; fcfg.shadow_scene_max[1] = 3; fcfg.shadow_scene_max[2] = 12;
    fcfg.dir_translucency = translucency ? true : false;   /* rpg-29zj API */
    fcfg.dir_caustics = caustics ? true : false;            /* rpg-kbqd API */

    render_forward_t fwd;
    ASSERT_TRUE(render_forward_init(&fwd, &fcfg));

    /* Offscreen target: a hidden window's backbuffer is UNDEFINED for both
     * rendering and readback, so draw into an FBO and read that instead
     * (render_forward restores the entry framebuffer for its main graph). */
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

    glViewport(0, 0, WIN, WIN);
    while (glGetError() != GL_NO_ERROR) { }   /* drain stale errors. */
    for (int frame = 0; frame < 2; ++frame) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        render_forward_render(&fwd, &scene);
        for (GLenum e; (e = glGetError()) != GL_NO_ERROR;)
            fprintf(stderr, "  GL error 0x%04x after frame %d\n", e, frame);
    }
    static uint8_t px[WIN * WIN * 3];
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glReadPixels(0, 0, WIN, WIN, GL_RGB, GL_UNSIGNED_BYTE, px);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    /* TSHADOW_DUMP: write the frame for visual inspection (debug aid). */
    if (getenv("TSHADOW_DUMP")) {
        char path[128];
        snprintf(path, sizeof path, "%s_%d.ppm", getenv("TSHADOW_DUMP"),
                 translucency);
        FILE *fp = fopen(path, "wb");
        if (fp) {
            fprintf(fp, "P6\n%d %d\n255\n", WIN, WIN);
            for (int y = WIN - 1; y >= 0; --y)
                fwrite(px + (size_t)y * WIN * 3u, 1, (size_t)WIN * 3u, fp);
            fclose(fp);
        }
    }

    /* Shadow displacement: panels at y=2, light travel (0.5,-1,0) => shadows
     * shift +1.0 in X. The PERSPECTIVE camera (14 up, ground half-width 5.8)
     * sees a y=2 panel cover ground out to 0.5 + (edge-0.5)*14/12, so sample
     * the shadow band OUTSIDE that cover. Glass x[-4,-1]: shadow x[-3,0],
     * cover to -1.25 -> sample (-0.3, 0). Opaque x[1.5,3.5]: shadow x[2.5,4.5],
     * cover to 4.0 -> sample (4.25, 0). Open sun: (-2.2, 4.5) (clear of both
     * panels, their shadows, and the perspective cover). */
    sample_ground(px, 0.5f, 0.0f, ch, fov, -2.2f, 4.5f, open_rgb);
    sample_ground(px, 0.5f, 0.0f, ch, fov, -0.3f, 0.0f, glass_rgb);
    sample_ground(px, 0.5f, 0.0f, ch, fov, 4.25f, 0.0f, opaque_rgb);

    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &col);
    glDeleteRenderbuffers(1, &dep);
    render_forward_destroy(&fwd);
    static_mesh_destroy(&ground);
    static_mesh_destroy(&glass);
    static_mesh_destroy(&panel);
    return 0;
}

int main(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 77;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    if (getenv("TSHADOW_GLDEBUG"))
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    SDL_Window *win = SDL_CreateWindow("translucent shadow", 0, 0, WIN, WIN,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!win) return 77;
    SDL_GLContext gc = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, gc);
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) return 77;
    gl_loader_t loader = { sdl_get_proc, NULL };

    if (getenv("TSHADOW_GLDEBUG")) {
        /* KHR_debug: print every driver-reported error with its message. */
        void (*cb_enable)(void) = NULL;
        void (*dbg_cb)(void *, void *) = NULL;
        (void)cb_enable; (void)dbg_cb;
        typedef void (*DEBUGPROC)(GLenum, GLenum, GLuint, GLenum, GLsizei,
                                  const GLchar *, const void *);
        void (*msg_cb)(DEBUGPROC, const void *) =
            (void (*)(DEBUGPROC, const void *))SDL_GL_GetProcAddress(
                "glDebugMessageCallback");
        if (msg_cb) {
            glEnable(0x92E0 /* GL_DEBUG_OUTPUT */);
            glEnable(0x8242 /* GL_DEBUG_OUTPUT_SYNCHRONOUS */);
            msg_cb((DEBUGPROC)0, NULL);
            extern void tshadow_debug_cb(GLenum, GLenum, GLuint, GLenum,
                                         GLsizei, const GLchar *,
                                         const void *);
            msg_cb(tshadow_debug_cb, NULL);
        }
    }

    int fails = 0;
    float open_t[3], glass_t[3], opaque_t[3];
    float open_f[3], glass_f[3], opaque_f[3];

    /* --- with the translucency mask ------------------------------------- */
    if (render_case(&loader, 1, 0, open_t, glass_t, opaque_t)) fails |= 1;
    if (!fails) {
        float lo = lum(open_t), lg = lum(glass_t), lp = lum(opaque_t);
        fprintf(stderr, "mask on : open=%.3f glass=%.3f opaque=%.3f "
                "(glass B/R %.2f vs open %.2f)\n", lo, lg, lp,
                glass_t[2] / (glass_t[0] + 1e-3f),
                open_t[2] / (open_t[0] + 1e-3f));
        /* open ground is sunlit */
        if (!(lo > 0.15f)) { fprintf(stderr, "  FAIL: open too dark\n"); fails |= 2; }
        /* glass shadow: attenuated but clearly lit (NOT a hard shadow) */
        if (!(lg > 0.18f * lo && lg < 0.92f * lo)) {
            fprintf(stderr, "  FAIL: glass shadow not translucent\n"); fails |= 4; }
        /* tint: transmitted light is blue-shifted vs open sun */
        if (!(glass_t[2] / (glass_t[0] + 1e-3f) >
              1.3f * open_t[2] / (open_t[0] + 1e-3f))) {
            fprintf(stderr, "  FAIL: glass shadow not tinted\n"); fails |= 8; }
        /* opaque shadow still dark (regression) */
        if (!(lp < 0.35f * lo)) {
            fprintf(stderr, "  FAIL: opaque shadow leaked\n"); fails |= 16; }
    }

    /* --- mask disabled: glass falls back to a hard caster ---------------- */
    if (render_case(&loader, 0, 0, open_f, glass_f, opaque_f)) fails |= 32;
    if (!(fails & 32)) {
        float lo = lum(open_f), lg = lum(glass_f);
        fprintf(stderr, "mask off: open=%.3f glass=%.3f\n", lo, lg);
        if (!(lg < 0.35f * lo)) {
            fprintf(stderr, "  FAIL: disabled mask should hard-shadow\n");
            fails |= 64; }
    }

    /* --- caustics on, no SDF set: the traced map redistributes nothing
     * (ortho rays land on their own texel), so the projected result must
     * MATCH the flat-tint path -- proving the caustic map is projected and
     * depth-gated in the forward pass (rpg-kbqd). Skipped when the context
     * lacks GL 4.3 compute (render_case then falls back to flat tint, which
     * satisfies the same assertions anyway). */
    {
        float open_c[3], glass_c[3], opaque_c[3];
        if (render_case(&loader, 1, 1, open_c, glass_c, opaque_c)) fails |= 128;
        if (!(fails & 128)) {
            float lo = lum(open_c), lg = lum(glass_c), lp = lum(opaque_c);
            fprintf(stderr, "caustics: open=%.3f glass=%.3f opaque=%.3f "
                    "(vs flat glass %.3f)\n", lo, lg, lp, lum(glass_t));
            if (!(lg > 0.18f * lo && lg < 0.92f * lo)) {
                fprintf(stderr, "  FAIL: caustic glass shadow broken\n");
                fails |= 256; }
            if (fabsf(lg - lum(glass_t)) > 0.08f) {
                fprintf(stderr, "  FAIL: identity caustic != flat tint\n");
                fails |= 512; }
            if (!(lp < 0.35f * lo)) {
                fprintf(stderr, "  FAIL: caustics leaked into opaque shadow\n");
                fails |= 1024; }
        }
    }

    fprintf(stderr, "translucent_shadow_tests: %s (0x%x)\n",
            fails ? "FAIL" : "all ok", fails);
    SDL_GL_DeleteContext(gc);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return fails;
}

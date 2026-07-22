/**
 * @file caustic_compute_tests.c
 * @brief TDD tests for the light-space caustics compute (rpg-kbqd): a GL 4.3
 *        compute pass consumes the CSM translucency mask (tint+coverage +
 *        linear distance), reconstructs each translucent texel's surface
 *        position along its light ray, traces jittered rays through the
 *        resident SDF chunks, and splats transmitted energy (r32ui fixed
 *        point, imageAtomicAdd) into a light-space caustic map, resolved to
 *        an RGBA16F array the receiver projects instead of the flat tint.
 *
 * Written before the implementation (TDD phase 1) -- drives the API:
 *   shadow_caustics_config_t / shadow_caustics_t
 *   shadow_caustics_init / destroy / set_sdf   (shadow_caustics_init.c)
 *   shadow_caustics_bake / bind                (shadow_caustics_bake.c)
 *
 * All cases run on a SYNTHETIC mask uploaded directly (no CSM needed): an
 * ortho light camera looking straight down -Y over world [-8,8]^2.
 *   happy: scatter 0, no SDF -> every ray continues along the light axis, so
 *          the resolved map EQUALS alpha*tint texel-for-texel;
 *   edge:  empty mask (alpha 0) -> map stays zero;
 *   edge:  scatter > 0 -> texels redistribute but TOTAL energy is conserved;
 *   fail:  init rejects NULL loader / zero resolution; bake on an
 *          out-of-range cascade leaves the map untouched.
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

#include "ferrum/math/mat4.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/shadow_caustics.h"

#define RES 64

#define ASSERT_TRUE(expr)                                                     \
    do { if (!(expr)) { fprintf(stderr, "  ASSERT_TRUE failed: %s (%s:%d)\n", \
        #expr, __FILE__, __LINE__); return 1; } } while (0)
#define ASSERT_NEAR(a, b, tol)                                                \
    do { float a_ = (a), b_ = (b);                                            \
        if (fabsf(a_ - b_) > (tol)) { fprintf(stderr,                         \
            "  ASSERT_NEAR failed: %g vs %g (tol %g) (%s:%d)\n",              \
            (double)a_, (double)b_, (double)(tol), __FILE__, __LINE__);       \
        return 1; } } while (0)

static void *sdl_get_proc(const char *n, void *u) { (void)u; return SDL_GL_GetProcAddress(n); }

/* Ortho light rig looking straight down -Y over world [-8,8]^2, eye 20 up,
 * far 40 -- mirrors the CSM's virtual-eye + normalised-distance convention. */
static const float LIGHT_EYE[3] = { 0.0f, 20.0f, 0.0f };
static const float LIGHT_FAR = 40.0f;

static mat4_t light_vp(void)
{
    /* Column-major ortho: x -> ndc.x/8, z -> ndc.y/8, y -> depth (-1 at eye
     * plane): world y=20 -> -1, y=-20 -> +1. */
    mat4_t m = mat4_identity();
    m.m[0] = 1.0f / 8.0f;  m.m[5] = 0.0f;          m.m[10] = 0.0f;
    m.m[9] = 1.0f / 8.0f;  /* z (col 2, row 1) -> ndc.y */
    m.m[6] = -1.0f / 20.0f; /* y (col 1, row 2) -> ndc.z */
    m.m[1] = m.m[2] = m.m[4] = m.m[8] = 0.0f;
    m.m[15] = 1.0f;
    return m;
}

/* Upload the synthetic mask pair: color RGBA16F array (tint+coverage) and
 * depth R32F array (normalised distance), 1 layer each. The glass pane sits
 * at world y=2 -> distance (20-2)/40 = 0.45 everywhere it exists. */
static void make_mask(GLuint *color, GLuint *depth, float alpha)
{
    static float col[RES * RES * 4];
    static float dep[RES * RES];
    for (int i = 0; i < RES * RES; ++i) {
        int x = i % RES, y = i / RES;
        /* Glass rect: central half of the map. */
        int in = (x >= RES / 4 && x < 3 * RES / 4 &&
                  y >= RES / 4 && y < 3 * RES / 4);
        col[i * 4 + 0] = in ? 0.1f : 0.0f;
        col[i * 4 + 1] = in ? 0.3f : 0.0f;
        col[i * 4 + 2] = in ? 0.9f : 0.0f;
        col[i * 4 + 3] = in ? alpha : 0.0f;
        dep[i] = in ? 0.45f : 1.0f;
    }
    glGenTextures(1, color);
    glBindTexture(GL_TEXTURE_2D_ARRAY, *color);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA16F, RES, RES, 1, 0,
                 GL_RGBA, GL_FLOAT, col);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenTextures(1, depth);
    glBindTexture(GL_TEXTURE_2D_ARRAY, *depth);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R32F, RES, RES, 1, 0,
                 GL_RED, GL_FLOAT, dep);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

/* Upload a 3D RGBA32F SDF "chunk" whose alpha holds the distance to a
 * horizontal floor plane at world y = floor_y (the gi_sdf_stream texture
 * convention: rgb = albedo, a = distance). The box spans origin..origin +
 * dims*voxel; distances are exact planes so the sphere-trace is precise. */
static GLuint make_floor_sdf(const float origin[3], const int dims[3],
                             float voxel, float floor_y)
{
    size_t n = (size_t)dims[0] * dims[1] * dims[2];
    float *tx = malloc(n * 4u * sizeof(float));
    if (tx == NULL) return 0;
    for (int z = 0; z < dims[2]; ++z)
        for (int y = 0; y < dims[1]; ++y)
            for (int x = 0; x < dims[0]; ++x) {
                size_t i = ((size_t)z * dims[1] + y) * dims[0] + x;
                float wy = origin[1] + ((float)y + 0.5f) * voxel;
                tx[i * 4 + 0] = 0.5f;
                tx[i * 4 + 1] = 0.5f;
                tx[i * 4 + 2] = 0.5f;
                tx[i * 4 + 3] = wy - floor_y;
            }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_3D, tex);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA32F, dims[0], dims[1], dims[2], 0,
                 GL_RGBA, GL_FLOAT, tx);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    free(tx);
    return tex;
}

/* Read the resolved caustic map (RGBA16F array layer 0) back as floats. */
static void read_map(const shadow_caustics_t *c, float *out /* RES*RES*4 */)
{
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              shadow_caustics_map_texture(c), 0, 0);
    glReadPixels(0, 0, RES, RES, GL_RGBA, GL_FLOAT, out);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
}

static int run_bake(shadow_caustics_t *c, GLuint mask_color, GLuint mask_depth,
                    uint32_t cascade)
{
    mat4_t vp = light_vp();
    while (glGetError() != GL_NO_ERROR) { }
    shadow_caustics_bake(c, mask_color, mask_depth, 0u, cascade, vp.m,
                         LIGHT_EYE, LIGHT_FAR);
    for (GLenum e; (e = glGetError()) != GL_NO_ERROR;)
        fprintf(stderr, "  GL error 0x%04x during bake\n", e);
    return 0;
}

static float sum_energy(const float *map, int ch)
{
    float s = 0.0f;
    for (int i = 0; i < RES * RES; ++i)
        s += map[i * 4 + ch];
    return s;
}

/* 5x5 patch mean around (x,y): the per-texel splat count is stochastic
 * (16 jittered rays), so single-texel reads carry ~20% noise; the patch mean
 * is what the (bilinear-filtered) receiver effectively sees. */
static float avg_patch(const float *map, int x, int y, int ch)
{
    float s = 0.0f;
    int n = 0;
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            int px = x + dx, py = y + dy;
            if (px < 0 || py < 0 || px >= RES || py >= RES) continue;
            s += map[(py * RES + px) * 4 + ch];
            ++n;
        }
    return n ? s / (float)n : 0.0f;
}

int main(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 77;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window *win = SDL_CreateWindow("caustics", 0, 0, 64, 64,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!win) return 77;
    SDL_GLContext gc = SDL_GL_CreateContext(win);
    if (!gc) { SDL_Quit(); return 77; }   /* no GL 4.3 -> skip. */
    SDL_GL_MakeCurrent(win, gc);
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) return 77;
    gl_loader_t loader = { sdl_get_proc, NULL };

    /* --- fail: bad configs are rejected up front ------------------------- */
    shadow_caustics_t c;
    {
        shadow_caustics_config_t bad;
        memset(&bad, 0, sizeof bad);
        bad.resolution = RES; bad.cascades = 1;
        ASSERT_TRUE(!shadow_caustics_init(&c, &bad));   /* NULL loader */
        bad.loader = &loader; bad.resolution = 0;
        ASSERT_TRUE(!shadow_caustics_init(&c, &bad));   /* zero res */
    }

    shadow_caustics_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.loader = &loader;
    cfg.resolution = RES;
    cfg.cascades = 1;
    cfg.samples = 16;
    cfg.scatter = 0.0f;        /* no scatter first: exact identity splat. */
    cfg.scatter_dist = 1.0f;
    cfg.max_dist = 64.0f;
    if (!shadow_caustics_init(&c, &cfg)) {
        fprintf(stderr, "caustic_compute_tests: no compute support, skip\n");
        SDL_Quit();
        return 77;
    }

    static float map[RES * RES * 4];
    GLuint mc = 0, md = 0;
    int fails = 0;

    /* --- happy: scatter 0, no SDF -> map == alpha*tint per texel --------- */
    make_mask(&mc, &md, 0.6f);
    run_bake(&c, mc, md, 0);
    read_map(&c, map);
    if (getenv("CAUSTIC_DEBUG")) {
        float s = 0, mx = 0; int ax = -1;
        for (int i = 0; i < RES * RES; ++i) {
            s += map[i * 4 + 2];
            if (map[i * 4 + 2] > mx) { mx = map[i * 4 + 2]; ax = i; }
        }
        fprintf(stderr, "  dbg: sumB=%.3f maxB=%.3f at (%d,%d)\n",
                s, mx, ax >= 0 ? ax % RES : -1, ax >= 0 ? ax / RES : -1);
        /* Readback sanity: clear the map layer via FBO and read it back. */
        GLuint f2 = 0;
        glGenFramebuffers(1, &f2);
        glBindFramebuffer(GL_FRAMEBUFFER, f2);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  shadow_caustics_map_texture(&c), 0, 0);
        glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        float probe[4] = { -1, -1, -1, -1 };
        glReadPixels(RES / 2, RES / 2, 1, 1, GL_RGBA, GL_FLOAT, probe);
        fprintf(stderr, "  dbg: readback sanity = %.2f %.2f %.2f (want .25 .5 .75)\n",
                probe[0], probe[1], probe[2]);
        /* Raw accumulator: read layer 2 (blue) as integers. */
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  c.accum_tex, 0, 2);
        GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        uint32_t praw[4] = { 9, 9, 9, 9 };
        glReadPixels(RES / 2, RES / 2, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, praw);
        fprintf(stderr, "  dbg: fb status 0x%04x accumB(center) = %u (want ~%u)\n",
                st, praw[0], (unsigned)(0.54f * 65536.0f));
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &f2);
        run_bake(&c, mc, md, 0);   /* re-bake: the sanity clear dirtied the map. */
        read_map(&c, map);
    }
    {
        /* Inside the glass rect: alpha*tint = 0.6*(0.1,0.3,0.9). Rays run
         * parallel to the ortho axis, so every texel lands on itself. */
        int center = (RES / 2) * RES + RES / 2;
        ASSERT_NEAR(map[center * 4 + 0], 0.06f, 0.01f);
        ASSERT_NEAR(map[center * 4 + 1], 0.18f, 0.01f);
        ASSERT_NEAR(map[center * 4 + 2], 0.54f, 0.02f);
        /* Outside: no glass, no energy. */
        int corner = 2 * RES + 2;
        ASSERT_NEAR(map[corner * 4 + 2], 0.0f, 1e-3f);
        fprintf(stderr, "scatter 0: center=(%.3f %.3f %.3f) ok\n",
                map[center * 4], map[center * 4 + 1], map[center * 4 + 2]);
    }
    float base_sum[3] = { sum_energy(map, 0), sum_energy(map, 1),
                          sum_energy(map, 2) };

    /* --- fail: out-of-range cascade leaves the map untouched ------------- */
    run_bake(&c, mc, md, 7);   /* only cascade 0 exists. */
    read_map(&c, map);
    ASSERT_NEAR(sum_energy(map, 2), base_sum[2], base_sum[2] * 0.01f + 0.01f);

    /* --- edge: empty mask -> zero map ------------------------------------ */
    {
        GLuint mc0 = 0, md0 = 0;
        make_mask(&mc0, &md0, 0.0f);
        run_bake(&c, mc0, md0, 0);
        read_map(&c, map);
        ASSERT_NEAR(sum_energy(map, 0), 0.0f, 1e-3f);
        ASSERT_NEAR(sum_energy(map, 2), 0.0f, 1e-3f);
        glDeleteTextures(1, &mc0);
        glDeleteTextures(1, &md0);
    }

    /* --- edge: scatter > 0 -> redistributed but ENERGY CONSERVED --------- */
    shadow_caustics_destroy(&c);
    cfg.scatter = 0.15f;       /* ~9.6 m footprint over the 64 m march. */
    ASSERT_TRUE(shadow_caustics_init(&c, &cfg));
    run_bake(&c, mc, md, 0);
    read_map(&c, map);
    {
        float s[3] = { sum_energy(map, 0), sum_energy(map, 1),
                       sum_energy(map, 2) };
        fprintf(stderr, "scatter %g: sum B %.2f vs base %.2f\n",
                (double)cfg.scatter, (double)s[2], (double)base_sum[2]);
        for (int ch = 0; ch < 3; ++ch)
            ASSERT_NEAR(s[ch], base_sum[ch],
                        base_sum[ch] * 0.03f + 0.05f);   /* 3% + fp slack. */
        /* And it actually scattered: the center texel lost energy. */
        int center = (RES / 2) * RES + RES / 2;
        ASSERT_TRUE(map[center * 4 + 2] < 0.54f * 0.9f ||
                    map[center * 4 + 2] > 0.54f * 1.1f);
    }

    /* --- SDF termination (rpg-39mc): a floor chunk 1 m under the glass ----
     * With scatter 0.15 the scatter disk radius is ~0.15 m at the floor (rays
     * stop after ~1 m) -- SUB-TEXEL, so a uniform glass interior stays at its
     * flat value. Without the floor the same rays flew 64 m and spread ~9.6 m
     * (asserted above). This distinguishes "rays hit the SDF" from "rays fly
     * to max_dist". */
    {
        static const float org[3] = { -8.0f, 0.0f, -8.0f };
        static const int dims[3] = { 64, 12, 64 };     /* 16 x 3 x 16 m @ 0.25. */
        GLuint floor_tex = make_floor_sdf(org, dims, 0.25f, 1.0f);
        ASSERT_TRUE(floor_tex != 0);
        uint32_t texs[1] = { floor_tex };
        float orgs[1][3] = { { org[0], org[1], org[2] } };
        float dimf[1][3] = { { 64.0f, 12.0f, 64.0f } };
        float voxs[1] = { 0.25f };
        shadow_caustics_set_sdf(&c, texs, orgs, dimf, voxs, 1u);
        run_bake(&c, mc, md, 0);
        read_map(&c, map);
        float cb = avg_patch(map, RES / 2, RES / 2, 2);
        float s2 = sum_energy(map, 2);
        fprintf(stderr, "floor chunk: center B=%.3f sum B=%.2f\n", cb, (double)s2);
        ASSERT_NEAR(cb, 0.54f, 0.05f);                    /* concentrated. */
        ASSERT_NEAR(s2, base_sum[2], base_sum[2] * 0.03f + 0.05f);
        shadow_caustics_set_sdf(&c, NULL, NULL, NULL, NULL, 0u);

        /* --- zone fallback: the same floor as the GLOBAL zone SDF, no fine
         * chunks resident -> rays must still terminate on it. */
        shadow_caustics_set_zone(&c, floor_tex, org, dimf[0], 0.25f);
        run_bake(&c, mc, md, 0);
        read_map(&c, map);
        cb = avg_patch(map, RES / 2, RES / 2, 2);
        fprintf(stderr, "zone floor : center B=%.3f\n", cb);
        ASSERT_NEAR(cb, 0.54f, 0.05f);

        /* --- page-fault semantics: a fine chunk that COVERS the volume but is
         * EMPTY (max positive distance) must mask the zone (the zone is only a
         * fallback where no fine chunk is resident, mirroring the GI trace) ->
         * rays fly to max_dist and the centre spreads out again. */
        static const float far_org[3] = { -8.0f, -20.0f, -8.0f };
        static const int far_dims[3] = { 16, 32, 16 };  /* floor way below. */
        GLuint empty_tex = make_floor_sdf(far_org, far_dims, 1.0f, -100.0f);
        ASSERT_TRUE(empty_tex != 0);
        uint32_t etexs[1] = { empty_tex };
        float eorgs[1][3] = { { -8.0f, -60.0f, -8.0f } };
        float edimf[1][3] = { { 16.0f, 32.0f, 16.0f } };   /* covers y -60..-28. */
        float evoxs[1] = { 1.0f };
        /* Chunk sits BELOW the whole march path but the zone still covers it:
         * instead make the chunk cover the march path with empty space. */
        eorgs[0][0] = -8.0f; eorgs[0][1] = -62.0f; eorgs[0][2] = -8.0f;
        (void)far_dims;
        /* Cover y in [-62, -30]: rays pass through it AFTER missing the zone
         * region? Simpler direct check: cover the SAME region as the zone with
         * empty distances -> cov=true everywhere the zone would have hit. */
        eorgs[0][0] = org[0]; eorgs[0][1] = org[1]; eorgs[0][2] = org[2];
        edimf[0][0] = 16.0f; edimf[0][1] = 3.0f; edimf[0][2] = 16.0f;
        evoxs[0] = 1.0f;
        GLuint cover_tex = make_floor_sdf(
            (const float[3]){ org[0], org[1], org[2] },
            (const int[3]){ 16, 3, 16 }, 1.0f, -100.0f);
        ASSERT_TRUE(cover_tex != 0);
        etexs[0] = cover_tex;
        shadow_caustics_set_sdf(&c, etexs, eorgs, edimf, evoxs, 1u);
        run_bake(&c, mc, md, 0);   /* zone floor still set from above. */
        read_map(&c, map);
        cb = avg_patch(map, RES / 2, RES / 2, 2);
        fprintf(stderr, "empty cover: center B=%.3f (must spread)\n", cb);
        ASSERT_TRUE(cb < 0.45f);
        shadow_caustics_set_sdf(&c, NULL, NULL, NULL, NULL, 0u);
        shadow_caustics_set_zone(&c, 0u, NULL, NULL, 0.0f);
        glDeleteTextures(1, &floor_tex);
        glDeleteTextures(1, &empty_tex);
        glDeleteTextures(1, &cover_tex);
    }

    shadow_caustics_destroy(&c);
    glDeleteTextures(1, &mc);
    glDeleteTextures(1, &md);
    fprintf(stderr, "caustic_compute_tests: %s\n", fails ? "FAIL" : "all ok");
    SDL_GL_DeleteContext(gc);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return fails;
}

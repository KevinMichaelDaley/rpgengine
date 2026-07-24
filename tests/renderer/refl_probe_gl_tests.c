/**
 * @file refl_probe_gl_tests.c
 * @brief GL-side reflection-probe tests (rpg-akwc) under headless EGL:
 *        the cube bake pass clears to the ambient (sky) radiance and reads
 *        back float faces, and the runtime uploader creates the atlas +
 *        meta TBO from a synthetic probe set. SKIPs cleanly without a GPU.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/renderer/egl_headless.h"
#include "ferrum/renderer/gi/refl_bake.h"
#include "ferrum/renderer/gi/refl_bind.h"

static int g_checks, g_fails;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_fails;                                                       \
            printf("  FAIL %s:%d %s\n", __func__, __LINE__, msg);            \
        }                                                                    \
    } while (0)

static void test_bake_empty_scene(const gl_loader_t *loader)
{
    enum { FR = 16 };
    refl_bake_t rb;
    CHECK(refl_bake_init(&rb, loader, FR), "bake init");

    render_scene_t scene;
    memset(&scene, 0, sizeof scene);      /* no items: pure sky clear. */

    refl_bake_params_t prm;
    memset(&prm, 0, sizeof prm);
    prm.sun_dir[1] = 1.0f;
    prm.sun_color[0] = prm.sun_color[1] = prm.sun_color[2] = 1.0f;
    prm.ambient[0] = 0.2f;
    prm.ambient[1] = 0.4f;
    prm.ambient[2] = 0.8f;

    static float mem[6][FR * FR * 4];
    float *faces[6] = { mem[0], mem[1], mem[2], mem[3], mem[4], mem[5] };
    float pos[3] = { 0, 0, 0 };
    refl_bake_probe(&rb, &scene, pos, &prm, 1.0f, faces, NULL);
    /* Every face texel = the ambient escape radiance, alpha 1. */
    int ok = 1;
    for (int f = 0; f < 6 && ok; ++f)
        for (int i = 0; i < FR * FR && ok; ++i)
            ok = fabsf(mem[f][i * 4 + 0] - 0.2f) < 1e-3f &&
                 fabsf(mem[f][i * 4 + 1] - 0.4f) < 1e-3f &&
                 fabsf(mem[f][i * 4 + 2] - 0.8f) < 1e-3f;
    CHECK(ok, "sky clear radiance on all faces");
    refl_bake_destroy(&rb);
}

static void test_gpu_upload(const gl_loader_t *loader)
{
    enum { N = 2, TR = 8, MIPS = 2 };
    refl_probe_t probes[N];
    refl_probe_set_t set;
    refl_probe_set_init(&set, probes, N);
    set.tile_res = TR;
    set.mips = MIPS;
    set.tiles_x = 2u;
    set.tiles_y = 1u;
    for (uint32_t i = 0; i < N; ++i) {
        set.probes[set.count].pos[0] = (float)i;
        set.probes[set.count].ao = 0.5f;
        set.probes[set.count].tile = i;
        set.count += 1u;
    }
    static float m0[16 * 8 * 4], m1[8 * 4 * 4];
    for (size_t i = 0; i < sizeof(m0) / sizeof(float); ++i) m0[i] = 0.5f;
    for (size_t i = 0; i < sizeof(m1) / sizeof(float); ++i) m1[i] = 0.25f;
    float *mips[REFL_PROBE_MAX_MIPS] = { m0, m1 };

    refl_gpu_t gpu;
    CHECK(refl_gpu_upload(&gpu, loader, &set, mips, NULL),
          "gpu upload");
    CHECK(gpu.count == N && gpu.mips == MIPS, "gpu mirror fields");
    CHECK(gpu.atlas != 0 && gpu.meta_tex != 0, "gl objects created");
    refl_gpu_destroy(&gpu);
    CHECK(gpu.count == 0u, "destroy resets");

    /* NULL/degenerate input: disabled, not crashed. */
    refl_gpu_t g2;
    CHECK(!refl_gpu_upload(&g2, loader, NULL, mips, NULL),
          "null set rejected");
    CHECK(g2.count == 0u, "failed upload leaves feature off");
}

int main(void)
{
    if (!egl_headless_init(3, 3)) {
        printf("  SKIP: no EGL/GL device for refl_probe_gl tests\n");
        return 0;
    }
    gl_loader_t loader = { egl_headless_getproc, NULL };
    test_bake_empty_scene(&loader);
    test_gpu_upload(&loader);
    egl_headless_shutdown();
    printf("  %s: %d checks, %d failures\n",
           g_fails ? "FAIL" : "OK", g_checks, g_fails);
    return g_fails ? 1 : 0;
}

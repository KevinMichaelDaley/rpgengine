#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"
#include "ferrum/renderer/debug_correction_lines.h"
#include "ferrum/renderer/debug_lines.h"

#define ASSERT_TRUE(cond)                                                                                \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

static int nearly_equal_f_(float a, float b, float eps) {
    return fabsf(a - b) <= eps;
}

static int nearly_equal_v3_(vec3_t a, vec3_t b, float eps) {
    return nearly_equal_f_(a.x, b.x, eps) && nearly_equal_f_(a.y, b.y, eps) && nearly_equal_f_(a.z, b.z, eps);
}

static int test_cube_correction_lines_translation_only_(void) {
    const vec3_t est_pos = {0.0f, 0.0f, 0.0f};
    const quat_t est_rot = {0.0f, 0.0f, 0.0f, 1.0f};

    const vec3_t true_pos = {1.0f, 0.0f, 0.0f};
    const quat_t true_rot = {0.0f, 0.0f, 0.0f, 1.0f};

    vec3_t verts[32];
    const size_t n = fr_debug_correction_lines_cube(est_pos, est_rot, true_pos, true_rot, 0.125f, verts, 32u);
    ASSERT_TRUE(n == 16u);

    for (size_t i = 0u; i < n; i += 2u) {
        const vec3_t a = verts[i + 0u];
        const vec3_t b = verts[i + 1u];

        ASSERT_TRUE(nearly_equal_f_(b.x - a.x, 1.0f, 1e-5f));
        ASSERT_TRUE(nearly_equal_f_(b.y - a.y, 0.0f, 1e-5f));
        ASSERT_TRUE(nearly_equal_f_(b.z - a.z, 0.0f, 1e-5f));

        ASSERT_TRUE(fabsf(a.x) <= 0.125f + 1e-5f);
        ASSERT_TRUE(fabsf(a.y) <= 0.125f + 1e-5f);
        ASSERT_TRUE(fabsf(a.z) <= 0.125f + 1e-5f);

        ASSERT_TRUE(fabsf(b.x - 1.0f) <= 0.125f + 1e-5f);
        ASSERT_TRUE(fabsf(b.y) <= 0.125f + 1e-5f);
        ASSERT_TRUE(fabsf(b.z) <= 0.125f + 1e-5f);
    }

    return 0;
}

static int test_debug_lines_store_ttl_(void) {
    fr_debug_line_t storage[8];
    fr_debug_lines_t lines;
    fr_debug_lines_init(&lines, storage, 8u);

    const vec3_t a = {0.0f, 0.0f, 0.0f};
    const vec3_t b = {1.0f, 0.0f, 0.0f};

    ASSERT_TRUE(fr_debug_lines_add(&lines, a, b, 10.0, 0.5));

    vec3_t verts[32];
    size_t out_count = 0u;

    ASSERT_TRUE(fr_debug_lines_collect_vertices(&lines, 10.25, verts, 32u, &out_count));
    ASSERT_TRUE(out_count == 2u);
    ASSERT_TRUE(nearly_equal_v3_(verts[0], a, 1e-6f));
    ASSERT_TRUE(nearly_equal_v3_(verts[1], b, 1e-6f));

    out_count = 0u;
    ASSERT_TRUE(fr_debug_lines_collect_vertices(&lines, 10.75, verts, 32u, &out_count));
    ASSERT_TRUE(out_count == 0u);

    return 0;
}

int main(void) {
    int rc = 0;
    rc |= test_cube_correction_lines_translation_only_();
    rc |= test_debug_lines_store_ttl_();

    if (rc == 0) {
        printf("p011_renderer_correction_debug_lines_tests: OK\n");
    }
    return rc;
}

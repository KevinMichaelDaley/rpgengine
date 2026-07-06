/**
 * @file srd_critic_tests.cpp
 * @brief Tests for the SRD critic system (AnalyticalCritic, TorchScriptCritic, C API).
 *
 * Non-static functions: 1 (main)
 *
 * Tests:
 *   - AnalyticalCritic: create/destroy, score returns scalar, gradient nonzero,
 *     penetration increases with overlap, min size zero when large, bounds zero
 *     when inside, bounds increases outside
 *   - TorchScriptCritic: null on missing file, null on corrupt file
 *   - C API: destroy null is safe
 */

#include "ferrum/procgen/srd/srd_critic.h"
#include "ferrum/procgen/srd/srd_room_type.h"

#include <torch/torch.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

/* ── Test harness ──────────────────────────────────────────────── */

static int g_pass = 0;
static int g_fail = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    printf("  %-55s ", #name); \
    name(); \
    printf("[PASS]\n"); \
    g_pass++; \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_fail++; return; \
    } \
} while (0)

#define ASSERT_FLOAT_GT(a, b) ASSERT((a) > (b))
#define ASSERT_FLOAT_LT(a, b) ASSERT((a) < (b))

/* ── AnalyticalCritic tests ───────────────────────────────────── */

/**
 * @brief Create an AnalyticalCritic via the C API, verify non-NULL, destroy.
 */
TEST(test_analytical_create_destroy) {
    srd_critic_t *c = srd_critic_create_analytical(20.0f, 20.0f);
    ASSERT(c != NULL);
    srd_critic_destroy(c);
}

/**
 * @brief Create critic, make a simple 3-box layout, call score,
 *        verify result is a scalar tensor (dim() == 0).
 */
TEST(test_analytical_score_returns_scalar) {
    using namespace ferrum::srd;

    AnalyticalCritic::Config cfg;
    cfg.layout_w = 20.0f;
    cfg.layout_h = 20.0f;
    AnalyticalCritic critic(cfg);

    /* 3 boxes: (cx, cz, hw, hd) */
    auto params = torch::tensor({
        {2.0f, 2.0f, 1.5f, 1.5f},
        {6.0f, 2.0f, 1.5f, 1.5f},
        {10.0f, 2.0f, 1.5f, 1.5f}
    });

    /* Room types: GENERIC, ENTRANCE, CORRIDOR */
    auto types = torch::tensor({
        (int64_t)SRD_ROOM_GENERIC,
        (int64_t)SRD_ROOM_ENTRANCE,
        (int64_t)SRD_ROOM_CORRIDOR
    });

    torch::Tensor loss = critic.score(params, types);
    ASSERT(loss.dim() == 0);
}

/**
 * @brief Create params with requires_grad=true, score, backward(),
 *        verify params.grad() has at least one non-zero element.
 */
TEST(test_analytical_gradient_nonzero) {
    using namespace ferrum::srd;

    AnalyticalCritic::Config cfg;
    cfg.layout_w = 20.0f;
    cfg.layout_h = 20.0f;
    AnalyticalCritic critic(cfg);

    /* Two boxes close together -- should produce gradients */
    auto params = torch::tensor({
        {3.0f, 3.0f, 2.0f, 2.0f},
        {6.0f, 3.0f, 2.0f, 2.0f}
    }, torch::requires_grad(true));

    auto types = torch::tensor({
        (int64_t)SRD_ROOM_GENERIC,
        (int64_t)SRD_ROOM_ENTRANCE
    });

    torch::Tensor loss = critic.score(params, types);
    loss.backward();

    ASSERT(params.grad().defined());
    /* At least one gradient element should be non-zero */
    float grad_abs_sum = params.grad().abs().sum().item<float>();
    ASSERT_FLOAT_GT(grad_abs_sum, 0.0f);
}

/**
 * @brief Two boxes: first non-overlapping (score1), then overlapping (score2).
 *        Verify score2 > score1 due to penetration penalty.
 */
TEST(test_analytical_penetration_increases_with_overlap) {
    using namespace ferrum::srd;

    AnalyticalCritic::Config cfg;
    cfg.layout_w = 40.0f;
    cfg.layout_h = 40.0f;
    /* Boost penetration weight, zero others to isolate the term */
    cfg.w_penetration  = 10.0f;
    cfg.w_min_size     = 0.0f;
    cfg.w_separation   = 0.0f;
    cfg.w_adjacency    = 0.0f;
    cfg.w_reachability = 0.0f;
    cfg.w_bounds       = 0.0f;
    AnalyticalCritic critic(cfg);

    auto types = torch::tensor({
        (int64_t)SRD_ROOM_GENERIC,
        (int64_t)SRD_ROOM_GENERIC
    });

    /* Non-overlapping: boxes far apart */
    auto params_no_overlap = torch::tensor({
        {5.0f,  5.0f,  2.0f, 2.0f},
        {15.0f, 5.0f,  2.0f, 2.0f}
    });
    float score1 = critic.score(params_no_overlap, types).item<float>();

    /* Overlapping: boxes on top of each other */
    auto params_overlap = torch::tensor({
        {5.0f, 5.0f, 2.0f, 2.0f},
        {6.0f, 5.0f, 2.0f, 2.0f}
    });
    float score2 = critic.score(params_overlap, types).item<float>();

    ASSERT_FLOAT_GT(score2, score1);
}

/**
 * @brief All boxes with hw, hd > min_room_size.
 *        Verify MinimumSize component is zero (total loss is small).
 */
TEST(test_analytical_min_size_zero_when_large) {
    using namespace ferrum::srd;

    AnalyticalCritic::Config cfg;
    cfg.layout_w       = 40.0f;
    cfg.layout_h       = 40.0f;
    cfg.min_room_size  = 1.0f;
    /* Isolate the min_size term */
    cfg.w_penetration  = 0.0f;
    cfg.w_min_size     = 1.0f;
    cfg.w_separation   = 0.0f;
    cfg.w_adjacency    = 0.0f;
    cfg.w_reachability = 0.0f;
    cfg.w_bounds       = 0.0f;
    AnalyticalCritic critic(cfg);

    /* All boxes well above min_room_size (hw=3, hd=3 >> 1.0) */
    auto params = torch::tensor({
        {5.0f,  5.0f,  3.0f, 3.0f},
        {15.0f, 5.0f,  3.0f, 3.0f},
        {25.0f, 5.0f,  3.0f, 3.0f}
    });
    auto types = torch::tensor({
        (int64_t)SRD_ROOM_GENERIC,
        (int64_t)SRD_ROOM_GENERIC,
        (int64_t)SRD_ROOM_GENERIC
    });

    float loss = critic.score(params, types).item<float>();
    /* MinimumSize = sum of max(min_size - hw, 0)^2 + max(min_size - hd, 0)^2
     * All hw, hd > min_size, so all clamped values are zero => loss = 0. */
    ASSERT_FLOAT_LT(loss, 1e-6f);
}

/**
 * @brief All boxes within [0, W] x [0, H].
 *        Verify BoundsViolation loss is small.
 */
TEST(test_analytical_bounds_zero_when_inside) {
    using namespace ferrum::srd;

    AnalyticalCritic::Config cfg;
    cfg.layout_w       = 20.0f;
    cfg.layout_h       = 20.0f;
    /* Isolate bounds term */
    cfg.w_penetration  = 0.0f;
    cfg.w_min_size     = 0.0f;
    cfg.w_separation   = 0.0f;
    cfg.w_adjacency    = 0.0f;
    cfg.w_reachability = 0.0f;
    cfg.w_bounds       = 1.0f;
    AnalyticalCritic critic(cfg);

    /* All boxes comfortably inside [0, 20] x [0, 20] */
    auto params = torch::tensor({
        {5.0f,  5.0f,  2.0f, 2.0f},
        {10.0f, 10.0f, 2.0f, 2.0f},
        {15.0f, 15.0f, 2.0f, 2.0f}
    });
    auto types = torch::tensor({
        (int64_t)SRD_ROOM_GENERIC,
        (int64_t)SRD_ROOM_GENERIC,
        (int64_t)SRD_ROOM_GENERIC
    });

    float loss = critic.score(params, types).item<float>();
    ASSERT_FLOAT_LT(loss, 1e-6f);
}

/**
 * @brief Box outside bounds has higher loss than a box inside bounds.
 */
TEST(test_analytical_bounds_increases_outside) {
    using namespace ferrum::srd;

    AnalyticalCritic::Config cfg;
    cfg.layout_w       = 20.0f;
    cfg.layout_h       = 20.0f;
    /* Isolate bounds term */
    cfg.w_penetration  = 0.0f;
    cfg.w_min_size     = 0.0f;
    cfg.w_separation   = 0.0f;
    cfg.w_adjacency    = 0.0f;
    cfg.w_reachability = 0.0f;
    cfg.w_bounds       = 1.0f;
    AnalyticalCritic critic(cfg);

    auto types = torch::tensor({(int64_t)SRD_ROOM_GENERIC});

    /* Inside bounds */
    auto params_inside = torch::tensor({{10.0f, 10.0f, 2.0f, 2.0f}});
    float loss_inside = critic.score(params_inside, types).item<float>();

    /* Outside bounds: centre at (-5, 10), left edge at -7 */
    auto params_outside = torch::tensor({{-5.0f, 10.0f, 2.0f, 2.0f}});
    float loss_outside = critic.score(params_outside, types).item<float>();

    ASSERT_FLOAT_GT(loss_outside, loss_inside);
}

/* ── TorchScriptCritic tests ─────────────────────────────────── */

/**
 * @brief Create with a bogus path. Verify returns NULL.
 */
TEST(test_torchscript_null_on_missing_file) {
    srd_critic_t *c = srd_critic_create_torchscript("/tmp/nonexistent_model_xyz123.pt");
    ASSERT(c == NULL);
}

/**
 * @brief Write garbage to a temp file. Verify returns NULL.
 */
TEST(test_torchscript_null_on_corrupt_file) {
    const char *path = "/tmp/srd_critic_test_corrupt.pt";
    {
        std::ofstream f(path, std::ios::binary);
        const char garbage[] = "THIS IS NOT A VALID TORCHSCRIPT MODEL";
        f.write(garbage, sizeof(garbage));
    }

    srd_critic_t *c = srd_critic_create_torchscript(path);
    ASSERT(c == NULL);

    /* Clean up temp file */
    std::remove(path);
}

/* ── C API tests ─────────────────────────────────────────────── */

/**
 * @brief srd_critic_destroy(NULL) should not crash.
 */
TEST(test_destroy_null_is_safe) {
    srd_critic_destroy(NULL);
    /* If we reach here, no crash occurred */
    ASSERT(true);
}

/* ── Main ─────────────────────────────────────────────────────── */

int main(void) {
    printf("=== SRD Critic Tests ===\n");

    /* AnalyticalCritic tests */
    RUN(test_analytical_create_destroy);
    RUN(test_analytical_score_returns_scalar);
    RUN(test_analytical_gradient_nonzero);
    RUN(test_analytical_penetration_increases_with_overlap);
    RUN(test_analytical_min_size_zero_when_large);
    RUN(test_analytical_bounds_zero_when_inside);
    RUN(test_analytical_bounds_increases_outside);

    /* TorchScriptCritic tests */
    RUN(test_torchscript_null_on_missing_file);
    RUN(test_torchscript_null_on_corrupt_file);

    /* C API tests */
    RUN(test_destroy_null_is_safe);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}

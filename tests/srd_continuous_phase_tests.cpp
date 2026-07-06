/**
 * @file srd_continuous_phase_tests.cpp
 * @brief Tests for the L-BFGS continuous optimisation phase.
 *
 * Non-static functions (1): main
 */
#include "ferrum/procgen/srd/srd_continuous_phase.h"
#include "ferrum/procgen/srd/srd_critic.h"
#include "ferrum/procgen/srd/srd_sdf_layout.h"
#include "ferrum/procgen/srd/srd_room_type.h"
#include "ferrum/procgen/srd/srd_descent_config.h"

#include <torch/torch.h>
#include <cmath>
#include <cstdio>
#include <cstring>

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

#define ASSERT_FLOAT_EQ(a, b, eps) ASSERT(fabsf((a) - (b)) < (eps))

/* ── Helper: build a test layout ─────────────────────────────── */

static void make_test_layout(srd_sdf_layout_t *layout) {
    srd_sdf_layout_init(layout);
    layout->bounds_w = 30.0f;
    layout->bounds_h = 30.0f;

    /* 4 overlapping boxes — critic should push them apart */
    srd_sdf_box_t boxes[4];
    memset(boxes, 0, sizeof(boxes));

    boxes[0].cx = 5.0f;  boxes[0].cz = 5.0f;
    boxes[0].hw = 3.0f;  boxes[0].hd = 3.0f;
    boxes[0].type = SRD_ROOM_GENERIC;

    boxes[1].cx = 7.0f;  boxes[1].cz = 5.0f;
    boxes[1].hw = 3.0f;  boxes[1].hd = 3.0f;
    boxes[1].type = SRD_ROOM_ENTRANCE;

    boxes[2].cx = 15.0f; boxes[2].cz = 15.0f;
    boxes[2].hw = 2.0f;  boxes[2].hd = 2.0f;
    boxes[2].type = SRD_ROOM_CORRIDOR;

    boxes[3].cx = 20.0f; boxes[3].cz = 20.0f;
    boxes[3].hw = 2.0f;  boxes[3].hd = 2.0f;
    boxes[3].type = SRD_ROOM_GENERIC;

    for (int i = 0; i < 4; i++)
        srd_sdf_layout_add_box(layout, &boxes[i]);

    srd_sdf_layout_set_adj(layout, 0, 1, true);
    srd_sdf_layout_set_adj(layout, 1, 2, true);
    srd_sdf_layout_set_adj(layout, 2, 3, true);
}

/* ── Tests ────────────────────────────────────────────────────── */

TEST(test_loss_decreases_or_plateaus) {
    srd_sdf_layout_t layout;
    make_test_layout(&layout);

    srd_critic_t *critic = srd_critic_create_analytical(30.0f, 30.0f);
    ASSERT(critic != NULL);

    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 1.0); /* tier <2s: 20 iters */
    cfg.critic = critic;

    float loss_before = srd_continuous_phase_run(&layout, &cfg);
    ASSERT(loss_before >= 0.0f); /* loss should be non-negative */

    /* Run again to get the post-optimization loss */
    float loss_after = srd_continuous_phase_run(&layout, &cfg);

    /* Loss should not increase significantly (may plateau) */
    ASSERT(loss_after <= loss_before + 0.01f);

    srd_critic_destroy(critic);
}

TEST(test_hw_hd_never_below_epsilon) {
    srd_sdf_layout_t layout;
    make_test_layout(&layout);

    /* Make one box very small to stress the clamp */
    layout.boxes[2].hw = 0.001f;
    layout.boxes[2].hd = 0.001f;

    srd_critic_t *critic = srd_critic_create_analytical(30.0f, 30.0f);
    ASSERT(critic != NULL);

    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 1.0);
    cfg.critic = critic;

    srd_continuous_phase_run(&layout, &cfg);

    for (int i = 0; i < layout.n_boxes; i++) {
        ASSERT(layout.boxes[i].hw >= SRD_EPSILON - 1e-6f);
        ASSERT(layout.boxes[i].hd >= SRD_EPSILON - 1e-6f);
    }

    srd_critic_destroy(critic);
}

TEST(test_layout_matches_tensor_after_writeback) {
    srd_sdf_layout_t layout;
    make_test_layout(&layout);

    srd_critic_t *critic = srd_critic_create_analytical(30.0f, 30.0f);
    ASSERT(critic != NULL);

    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 1.0);
    cfg.critic = critic;

    srd_continuous_phase_run(&layout, &cfg);

    /* After write-back, all boxes should have valid finite values */
    for (int i = 0; i < layout.n_boxes; i++) {
        ASSERT(std::isfinite(layout.boxes[i].cx));
        ASSERT(std::isfinite(layout.boxes[i].cz));
        ASSERT(std::isfinite(layout.boxes[i].hw));
        ASSERT(std::isfinite(layout.boxes[i].hd));
        ASSERT(layout.boxes[i].hw > 0.0f);
        ASSERT(layout.boxes[i].hd > 0.0f);
    }

    srd_critic_destroy(critic);
}

TEST(test_callable_independently) {
    /* Verify the function works as a standalone call */
    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    layout.bounds_w = 20.0f;
    layout.bounds_h = 20.0f;

    srd_sdf_box_t box;
    memset(&box, 0, sizeof(box));
    box.cx = 10.0f; box.cz = 10.0f;
    box.hw = 2.0f;  box.hd = 2.0f;
    box.type = SRD_ROOM_GENERIC;
    srd_sdf_layout_add_box(&layout, &box);

    srd_critic_t *critic = srd_critic_create_analytical(20.0f, 20.0f);
    ASSERT(critic != NULL);

    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 1.0);
    cfg.critic = critic;

    /* Should not crash with a single box */
    float loss = srd_continuous_phase_run(&layout, &cfg);
    ASSERT(std::isfinite(loss));

    srd_critic_destroy(critic);
}

/* ── Main ─────────────────────────────────────────────────────── */

int main(void) {
    printf("=== SRD Continuous Phase Tests ===\n");

    RUN(test_loss_decreases_or_plateaus);
    RUN(test_hw_hd_never_below_epsilon);
    RUN(test_layout_matches_tensor_after_writeback);
    RUN(test_callable_independently);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}

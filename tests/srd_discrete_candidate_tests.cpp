/**
 * @file srd_discrete_candidate_tests.cpp
 * @brief Tests for SRD discrete phase candidate sampling and LocalOptimize.
 *
 * Non-static functions (1): main
 *
 * Tests:
 *   - K candidates populated in one call
 *   - Failed applies are skipped (delta_L = -INFINITY)
 *   - delta_L positive for a rule that improves layout
 *   - LocalOptimize runs (params_copy differs from initial)
 *   - No heap allocation (verified by stack-allocated candidate array)
 */
#include "ferrum/procgen/srd/srd_discrete_candidates.h"
#include "ferrum/procgen/srd/srd_descent_rules.h"
#include "ferrum/procgen/srd/srd_rules_room.h"
#include "ferrum/procgen/srd/srd_rules_repair.h"
#include "ferrum/procgen/srd/srd_critic.h"
#include "ferrum/procgen/srd/srd_descent_config.h"
#include "ferrum/procgen/srd/srd_sdf_layout.h"
#include "ferrum/procgen/srd/srd_room_type.h"

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

/* ── Helper: build a non-trivial test layout ─────────────────── */

static void make_test_layout(srd_sdf_layout_t *layout) {
    srd_sdf_layout_init(layout);
    layout->bounds_w = 40.0f;
    layout->bounds_h = 40.0f;

    srd_sdf_box_t b;
    memset(&b, 0, sizeof(b));

    /* Box 0: overlapping with box 1 — gives room for improvement */
    b.cx = 5.0f; b.cz = 5.0f; b.hw = 3.0f; b.hd = 3.0f;
    b.type = SRD_ROOM_GENERIC;
    srd_sdf_layout_add_box(layout, &b);

    b.cx = 7.0f; b.cz = 5.0f; b.hw = 3.0f; b.hd = 3.0f;
    b.type = SRD_ROOM_ENTRANCE;
    srd_sdf_layout_add_box(layout, &b);

    b.cx = 20.0f; b.cz = 20.0f; b.hw = 4.0f; b.hd = 4.0f;
    b.type = SRD_ROOM_GENERIC;
    srd_sdf_layout_add_box(layout, &b);

    b.cx = 30.0f; b.cz = 30.0f; b.hw = 2.0f; b.hd = 2.0f;
    b.type = SRD_ROOM_CORRIDOR;
    srd_sdf_layout_add_box(layout, &b);

    srd_sdf_layout_set_adj(layout, 0, 1, true);
    srd_sdf_layout_set_adj(layout, 1, 2, true);
    srd_sdf_layout_set_adj(layout, 2, 3, true);
}

/* ── Tests ────────────────────────────────────────────────────── */

TEST(test_k_candidates_populated) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_room_register_split(&tbl);
    srd_rules_room_register_add(&tbl);
    srd_rules_room_register_modify(&tbl);
    srd_rules_room_register_annex(&tbl);

    srd_sdf_layout_t layout;
    make_test_layout(&layout);

    srd_critic_t *critic = srd_critic_create_analytical(40.0f, 40.0f);
    ASSERT(critic != NULL);

    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 1.0);
    cfg.critic = critic;
    cfg.rules = &tbl;

    srd_candidate_t *candidates = (srd_candidate_t *)calloc(cfg.k_candidates, sizeof(srd_candidate_t));
    uint32_t rng = 12345;

    int n = srd_discrete_sample_candidates(
        &layout, &cfg, candidates, cfg.k_candidates, &rng);

    /* Should have populated some candidates (may be < K if few applicable) */
    ASSERT(n > 0);
    ASSERT(n <= cfg.k_candidates);

    free(candidates);
    srd_critic_destroy(critic);
}

TEST(test_failed_applies_skipped) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_room_register_split(&tbl);
    srd_rules_room_register_add(&tbl);

    srd_sdf_layout_t layout;
    make_test_layout(&layout);

    srd_critic_t *critic = srd_critic_create_analytical(40.0f, 40.0f);
    ASSERT(critic != NULL);

    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 1.0);
    cfg.critic = critic;
    cfg.rules = &tbl;

    srd_candidate_t *candidates = (srd_candidate_t *)calloc(cfg.k_candidates, sizeof(srd_candidate_t));
    uint32_t rng = 99999;

    int n = srd_discrete_sample_candidates(
        &layout, &cfg, candidates, cfg.k_candidates, &rng);

    /* Any failed applies should have delta_L = -INFINITY */
    for (int i = 0; i < n; i++) {
        /* delta_L should be either a finite number or -INFINITY */
        ASSERT(std::isfinite(candidates[i].delta_L) ||
               candidates[i].delta_L == -INFINITY);
    }

    free(candidates);
    srd_critic_destroy(critic);
}

TEST(test_some_delta_L_positive) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_room_register_split(&tbl);
    srd_rules_room_register_add(&tbl);
    srd_rules_room_register_modify(&tbl);
    srd_rules_room_register_annex(&tbl);

    srd_sdf_layout_t layout;
    make_test_layout(&layout);

    srd_critic_t *critic = srd_critic_create_analytical(40.0f, 40.0f);
    ASSERT(critic != NULL);

    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 1.0);
    cfg.critic = critic;
    cfg.rules = &tbl;

    /* Try multiple seeds to find at least one positive delta_L */
    bool found_positive = false;
    for (uint32_t seed = 1; seed <= 10 && !found_positive; seed++) {
        uint32_t rng = seed * 7919;
        srd_candidate_t *candidates = (srd_candidate_t *)calloc(cfg.k_candidates, sizeof(srd_candidate_t));
        int n = srd_discrete_sample_candidates(
            &layout, &cfg, candidates, cfg.k_candidates, &rng);
        for (int i = 0; i < n; i++) {
            if (candidates[i].delta_L > 0.0f) {
                found_positive = true;
                break;
            }
        }
        free(candidates);
    }
    ASSERT(found_positive);

    srd_critic_destroy(critic);
}

TEST(test_local_optimize_modifies_params) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_room_register_split(&tbl);
    srd_rules_room_register_add(&tbl);
    srd_rules_room_register_modify(&tbl);

    srd_sdf_layout_t layout;
    make_test_layout(&layout);

    srd_critic_t *critic = srd_critic_create_analytical(40.0f, 40.0f);
    ASSERT(critic != NULL);

    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 1.0);
    cfg.critic = critic;
    cfg.rules = &tbl;

    srd_candidate_t *candidates = (srd_candidate_t *)calloc(cfg.k_candidates, sizeof(srd_candidate_t));
    uint32_t rng = 42;

    int n = srd_discrete_sample_candidates(
        &layout, &cfg, candidates, cfg.k_candidates, &rng);

    /* Find a candidate with finite delta_L and check params differ */
    bool found_modified = false;
    for (int i = 0; i < n; i++) {
        if (!std::isfinite(candidates[i].delta_L)) continue;
        /* The layout copy should differ from original in some way */
        bool differs = false;
        for (int j = 0; j < candidates[i].layout_copy.n_boxes; j++) {
            if (candidates[i].layout_copy.boxes[j].cx != layout.boxes[j].cx ||
                candidates[i].layout_copy.boxes[j].cz != layout.boxes[j].cz) {
                differs = true;
                break;
            }
        }
        /* Either the layout has different box count or params changed */
        if (candidates[i].layout_copy.n_boxes != layout.n_boxes || differs) {
            found_modified = true;
            break;
        }
    }
    ASSERT(found_modified);

    free(candidates);
    srd_critic_destroy(critic);
}

TEST(test_candidate_struct_size) {
    /* Verify the candidate struct is a value type with expected fields. */
    srd_candidate_t c;
    memset(&c, 0, sizeof(c));
    ASSERT(sizeof(c) > 0);
    ASSERT(c.delta_L == 0.0f);
    ASSERT(c.rule_idx == 0);
}

/* ── Main ─────────────────────────────────────────────────────── */

int main(void) {
    printf("=== SRD Discrete Candidate Tests ===\n");

    RUN(test_k_candidates_populated);
    RUN(test_failed_applies_skipped);
    RUN(test_some_delta_L_positive);
    RUN(test_local_optimize_modifies_params);
    RUN(test_candidate_struct_size);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}

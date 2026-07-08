/**
 * @file srd_descent_loop_bench.cpp
 * @brief Timing benchmark + test assertions for SRD loop components.
 *
 * Measures each phase independently to identify bottlenecks,
 * then runs assertions to verify correctness.
 *
 * Non-static functions (1): main
 */
#include "ferrum/procgen/srd/srd_descent_loop.h"
#include "ferrum/procgen/srd/srd_discrete_candidates.h"
#include "ferrum/procgen/srd/srd_continuous_phase.h"
#include "ferrum/procgen/srd/srd_descent_config.h"
#include "ferrum/procgen/srd/srd_sdf_layout.h"
#include "ferrum/procgen/srd/srd_room_type.h"
#include "ferrum/procgen/srd/srd_critic.h"
#include "ferrum/procgen/srd/srd_rules_room.h"
#include "ferrum/procgen/srd/srd_rules_repair.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void make_layout(srd_sdf_layout_t *layout) {
    srd_sdf_layout_init(layout);
    layout->bounds_w = 40.0f;
    layout->bounds_h = 40.0f;
    srd_sdf_box_t b;
    memset(&b, 0, sizeof(b));
    b.cx = 5; b.cz = 5; b.hw = 3; b.hd = 3; b.type = SRD_ROOM_GENERIC;
    srd_sdf_layout_add_box(layout, &b);
    b.cx = 7; b.cz = 5; b.type = SRD_ROOM_ENTRANCE;
    srd_sdf_layout_add_box(layout, &b);
    b.cx = 20; b.cz = 20; b.hw = 4; b.hd = 4; b.type = SRD_ROOM_GENERIC;
    srd_sdf_layout_add_box(layout, &b);
    b.cx = 30; b.cz = 30; b.hw = 2; b.hd = 2; b.type = SRD_ROOM_CORRIDOR;
    srd_sdf_layout_add_box(layout, &b);
    srd_sdf_layout_set_adj(layout, 0, 1, true);
    srd_sdf_layout_set_adj(layout, 1, 2, true);
    srd_sdf_layout_set_adj(layout, 2, 3, true);
}

int main(void) {
    srd_sdf_layout_t layout;
    make_layout(&layout);

    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_room_register_split(&tbl);
    srd_rules_room_register_modify(&tbl);
    srd_rules_repair_register(&tbl);
    printf("Rules registered: %d\n", tbl.n_rules);

    srd_critic_t *critic = srd_critic_create_analytical(40.0f, 40.0f);

    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 0.5);
    cfg.k_candidates = 4;
    cfg.lbfgs_max_iter = 3;
    cfg.local_optimize_steps = 1;
    cfg.continuous_steps_per_rewrite = 1;
    cfg.critic = critic;
    cfg.rules = &tbl;

    /* 1. Continuous phase */
    double t = now_s();
    float loss = srd_continuous_phase_run(&layout, &cfg);
    printf("Continuous phase: loss=%.4f  elapsed=%.3f s\n", loss, now_s() - t);

    /* 2. Candidate sampling */
    srd_candidate_t *cands =
        (srd_candidate_t *)calloc(4, sizeof(srd_candidate_t));
    uint32_t rng = 42;
    t = now_s();
    int n = srd_discrete_sample_candidates(&layout, &cfg, cands, 4, &rng);
    printf("Candidate sampling (K=4): n=%d  elapsed=%.3f s\n", n, now_s() - t);
    for (int i = 0; i < n; i++)
        printf("  cand[%d]: rule=%d delta_L=%.4f\n",
               i, cands[i].rule_idx, cands[i].delta_L);
    free(cands);

    /* 3. Full loop */
    make_layout(&layout);
    t = now_s();
    srd_descent_result_t result = srd_descent_optimize(&layout, &cfg);
    double elapsed = now_s() - t;
    printf("Full loop: loss=%.4f->%.4f  iters=%d  elapsed=%.3f s\n",
           result.initial_loss, result.final_loss,
           result.iterations, elapsed);

    /* -- Test assertions -- */
    int failures = 0;

    /* T1: terminates within budget */
    if (elapsed > 1.0) {
        printf("[FAIL] elapsed %.3f > 1.0 s\n", elapsed);
        failures++;
    }
    if (result.final_loss < 0.0f) {
        printf("[FAIL] final_loss %.4f < 0\n", result.final_loss);
        failures++;
    }

    /* T2: loss does not explode (with all rules including annex) */
    {
        srd_rule_table_t tbl2;
        srd_rule_table_init(&tbl2);
        srd_rules_room_register_split(&tbl2);
        srd_rules_room_register_modify(&tbl2);
        srd_rules_room_register_annex(&tbl2);
        srd_rules_repair_register(&tbl2);

        srd_descent_config_t cfg2;
        srd_descent_config_from_budget(&cfg2, 0.5);
        cfg2.k_candidates = 2;
        cfg2.lbfgs_max_iter = 1;
        cfg2.local_optimize_steps = 1;
        cfg2.continuous_steps_per_rewrite = 1;
        cfg2.critic = critic;
        cfg2.rules = &tbl2;

        srd_sdf_layout_t layout2;
        make_layout(&layout2);
        srd_descent_result_t r2 = srd_descent_optimize(&layout2, &cfg2);
        printf("All-rules loop: loss=%.4f->%.4f  iters=%d  boxes=%d\n",
               r2.initial_loss, r2.final_loss, r2.iterations,
               layout2.n_boxes);
        if (r2.final_loss > r2.initial_loss + 1.0f) {
            printf("[FAIL] loss increased: %.4f -> %.4f\n",
                    r2.initial_loss, r2.final_loss);
            failures++;
        }
    }

    /* T3: temperature decreases */
    if (result.iterations > 0) {
        if (result.final_temperature >= cfg.temperature_init) {
            printf("[FAIL] temp did not decrease: %.4f >= %.4f\n",
                   result.final_temperature, cfg.temperature_init);
            failures++;
        }
        if (result.final_temperature < cfg.temperature_min) {
            printf("[FAIL] temp below min: %.4f < %.4f\n",
                   result.final_temperature, cfg.temperature_min);
            failures++;
        }
    }

    srd_critic_destroy(critic);

    if (failures == 0) {
        printf("\nAll loop tests passed.\n");
    } else {
        printf("\n%d test(s) FAILED.\n", failures);
    }
    return failures > 0 ? 1 : 0;
}

/**
 * @file srd_descent_loop_minimal.cpp
 * @brief Minimal test to debug hang.
 */
#include "ferrum/procgen/srd/srd_descent_loop.h"
#include "ferrum/procgen/srd/srd_descent_config.h"
#include "ferrum/procgen/srd/srd_sdf_layout.h"
#include "ferrum/procgen/srd/srd_room_type.h"
#include "ferrum/procgen/srd/srd_critic.h"
#include "ferrum/procgen/srd/srd_rules_room.h"
#include "ferrum/procgen/srd/srd_rules_repair.h"
#include <cstdio>
#include <cstring>
#include <ctime>

int main(void) {
    printf("Starting...\n");

    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    layout.bounds_w = 40.0f;
    layout.bounds_h = 40.0f;
    srd_sdf_box_t b;
    memset(&b, 0, sizeof(b));
    b.cx = 5; b.cz = 5; b.hw = 3; b.hd = 3; b.type = SRD_ROOM_GENERIC;
    srd_sdf_layout_add_box(&layout, &b);
    b.cx = 20; b.cz = 20; b.hw = 4; b.hd = 4;
    srd_sdf_layout_add_box(&layout, &b);
    srd_sdf_layout_set_adj(&layout, 0, 1, true);

    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_room_register_split(&tbl);
    srd_rules_room_register_modify(&tbl);
    srd_rules_room_register_annex(&tbl);
    srd_rules_repair_register(&tbl);
    printf("Rules: %d\n", tbl.n_rules);

    srd_critic_t *critic = srd_critic_create_analytical(40.0f, 40.0f);
    printf("Critic: %p\n", (void*)critic);

    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 0.5);
    cfg.k_candidates = 2;
    cfg.lbfgs_max_iter = 1;
    cfg.local_optimize_steps = 1;
    cfg.continuous_steps_per_rewrite = 1;
    cfg.critic = critic;
    cfg.rules = &tbl;

    printf("Running optimize...\n");
    srd_descent_result_t result = srd_descent_optimize(&layout, &cfg);
    printf("Done: loss=%.4f->%.4f iters=%d\n",
           result.initial_loss, result.final_loss, result.iterations);

    srd_critic_destroy(critic);
    return 0;
}

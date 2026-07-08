/**
 * @file srd_descent_loop.cpp
 * @brief Main SRD descent outer loop (Algorithm 2, Kodnongbua et al.).
 *
 * Alternates continuous L-BFGS phases with discrete K-candidate rewrite
 * phases, applying temperature annealing and repair rules. Terminates
 * when time_budget_s is exhausted.
 *
 * Non-static functions (1): srd_descent_optimize
 */
#include "ferrum/procgen/srd/srd_descent_loop.h"
#include "ferrum/procgen/srd/srd_continuous_phase.h"
#include "ferrum/procgen/srd/srd_discrete_candidates.h"
#include "ferrum/procgen/srd/srd_discrete_maxcover.h"
#include "ferrum/procgen/srd/srd_discrete_repair.h"
#include "ferrum/procgen/srd/srd_critic.h"
#include "ferrum/procgen/srd/srd_room_type.h"

#include <torch/torch.h>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cmath>

/* ── srd_critic internal access ──────────────────────────────── */

struct srd_critic {
    ferrum::srd::ISrdCritic *impl;
};

/* ── Helpers ─────────────────────────────────────────────────── */

/** @brief Get elapsed seconds since t0. */
static double elapsed_since(const struct timespec *t0) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - t0->tv_sec) +
           (double)(now.tv_nsec - t0->tv_nsec) * 1e-9;
}

/**
 * @brief Score a layout using the critic (C++ side).
 *
 * Builds tensors from the layout and calls the critic's score().
 * Returns the scalar loss value.
 */
static float score_layout(const srd_sdf_layout_t *layout,
                          const srd_critic_t *critic) {
    const int n = layout->n_boxes;
    if (n < 1 || !critic || !critic->impl) return 0.0f;

    auto params = torch::zeros({n, 4}, torch::kFloat32);
    auto params_acc = params.accessor<float, 2>();
    for (int i = 0; i < n; i++) {
        params_acc[i][0] = layout->boxes[i].cx;
        params_acc[i][1] = layout->boxes[i].cz;
        params_acc[i][2] = layout->boxes[i].hw;
        params_acc[i][3] = layout->boxes[i].hd;
    }

    auto types = torch::zeros({n}, torch::kInt64);
    auto types_acc = types.accessor<int64_t, 1>();
    for (int i = 0; i < n; i++) {
        types_acc[i] = static_cast<int64_t>(layout->boxes[i].type);
    }

    torch::NoGradGuard no_grad;
    torch::Tensor loss = critic->impl->score(params, types);
    return loss.item<float>();
}

/* ── Public API ────────────────────────────────────────────────── */

extern "C" srd_descent_result_t srd_descent_optimize(
        srd_sdf_layout_t *layout,
        const srd_descent_config_t *cfg) {

    srd_descent_result_t result;
    memset(&result, 0, sizeof(result));

    if (!layout || !cfg || !cfg->critic || !cfg->rules) {
        result.final_loss = -1.0f;
        return result;
    }

    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    /* Measure initial loss */
    result.initial_loss = score_layout(layout, cfg->critic);
    float current_loss = result.initial_loss;
    float temperature = cfg->temperature_init;
    int iteration = 0;

    /* RNG state for discrete phase */
    uint32_t rng = 42u ^ (uint32_t)(cfg->time_budget_s * 1e6);
    if (rng == 0) rng = 1;

    /* Cap box count to prevent O(n^2) blowup in critic/L-BFGS.
     * With annex rules, boxes grow each iteration; beyond this
     * threshold the continuous phase becomes prohibitively expensive. */
    const int max_boxes_for_lbfgs = 64;

    while (elapsed_since(&t0) < cfg->time_budget_s) {
        fprintf(stderr, "[loop] iter=%d boxes=%d t=%.3f\n",
                iteration, layout->n_boxes, elapsed_since(&t0));
        /* Phase 1: Continuous optimisation (L-BFGS)
         * Skip if box count has grown too large — L-BFGS cost is
         * O(n^2) due to pairwise critic terms. */
        if (layout->n_boxes <= max_boxes_for_lbfgs) {
            for (int c = 0; c < cfg->continuous_steps_per_rewrite; c++) {
                if (elapsed_since(&t0) >= cfg->time_budget_s) break;
                float loss = srd_continuous_phase_run(layout, cfg);
                if (loss >= 0.0f) current_loss = loss;
            }
        }

        if (elapsed_since(&t0) >= cfg->time_budget_s) break;

        /* Phase 2: Discrete rewrite (K-candidates + max-cover)
         * Skip if less than 10% budget remains — candidate sampling
         * is expensive and should not overshoot.
         * Also skip if box count exceeds threshold — candidate layout
         * copies are ~260 KB each and SGD cost scales with n_boxes. */
        double remaining = cfg->time_budget_s - elapsed_since(&t0);
        if (remaining < cfg->time_budget_s * 0.1) break;
        if (layout->n_boxes > max_boxes_for_lbfgs) break;

        int k = cfg->k_candidates;
        srd_candidate_t *candidates =
            (srd_candidate_t *)calloc((size_t)k, sizeof(srd_candidate_t));
        if (!candidates) break;

        int n_cands = srd_discrete_sample_candidates(
            layout, cfg, candidates, k, &rng);

        if (n_cands > 0) {
            /* Build compatibility matrix */
            int stride = n_cands;
            uint8_t *compat =
                (uint8_t *)calloc((size_t)(n_cands * stride), sizeof(uint8_t));
            if (compat) {
                srd_build_compatibility(
                    cfg->rules, candidates, n_cands, layout, compat, stride);

                /* Greedy max-cover selection */
                int *selected = (int *)calloc((size_t)n_cands, sizeof(int));
                if (selected) {
                    int n_selected = srd_greedy_max_cover(
                        candidates, n_cands, compat, stride,
                        selected, n_cands);

                    /* Apply selected candidates to the layout */
                    for (int s = 0; s < n_selected; s++) {
                        int idx = selected[s];
                        srd_sdf_layout_copy(layout,
                                            &candidates[idx].layout_copy);
                    }

                    free(selected);
                }
                free(compat);
            }
        }

        free(candidates);

        fprintf(stderr, "  discrete done t=%.3f\n", elapsed_since(&t0));
        /* Phase 3: Repair */
        srd_apply_repairs(layout, cfg->rules);
        fprintf(stderr, "  repair done t=%.3f boxes=%d\n", elapsed_since(&t0), layout->n_boxes);

        /* Measure current loss after this iteration */
        current_loss = score_layout(layout, cfg->critic);

        /* Phase 4: Temperature annealing */
        temperature *= cfg->temperature_decay;
        if (temperature < cfg->temperature_min) {
            temperature = cfg->temperature_min;
        }

        iteration++;
    }

    result.final_loss = current_loss;
    result.final_temperature = temperature;
    result.iterations = iteration;
    return result;
}

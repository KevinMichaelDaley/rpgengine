/**
 * @file srd_discrete_candidates.cpp
 * @brief Candidate sampling and LocalOptimize for the SRD discrete phase.
 *
 * Samples K rule/selection pairs, applies each to a layout copy, runs
 * a short L-BFGS pass, and records delta_L. No heap allocation inside
 * the per-candidate loop (layout copies are value types).
 *
 * Non-static functions (1): srd_discrete_sample_candidates
 */
#include "ferrum/procgen/srd/srd_discrete_candidates.h"
#include "ferrum/procgen/srd/srd_critic.h"
#include "ferrum/procgen/srd/srd_room_type.h"

#include <torch/torch.h>
#include <cmath>
#include <cstring>

/* ── srd_critic internal access ──────────────────────────────── */

struct srd_critic {
    ferrum::srd::ISrdCritic *impl;
};

/* ── Helpers ─────────────────────────────────────────────────── */

/** xorshift32 RNG — same as in srd_descent_rules.c */
static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/**
 * @brief Extract layout box params into a [N,4] tensor and types into [N].
 */
static void layout_to_tensors(const srd_sdf_layout_t *layout,
                              torch::Tensor &params_out,
                              torch::Tensor &types_out) {
    const int n = layout->n_boxes;
    params_out = torch::zeros({n, 4}, torch::kFloat32);
    types_out  = torch::zeros({n}, torch::kInt64);

    auto p = params_out.accessor<float, 2>();
    auto t = types_out.accessor<int64_t, 1>();
    for (int i = 0; i < n; i++) {
        p[i][0] = layout->boxes[i].cx;
        p[i][1] = layout->boxes[i].cz;
        p[i][2] = layout->boxes[i].hw;
        p[i][3] = layout->boxes[i].hd;
        t[i]    = static_cast<int64_t>(layout->boxes[i].type);
    }
}

/**
 * @brief Write tensor params back into layout boxes, clamping hw/hd.
 */
static void tensors_to_layout(srd_sdf_layout_t *layout,
                              const torch::Tensor &params) {
    auto p = params.accessor<float, 2>();
    for (int i = 0; i < layout->n_boxes; i++) {
        layout->boxes[i].cx = p[i][0];
        layout->boxes[i].cz = p[i][1];
        layout->boxes[i].hw = (p[i][2] < SRD_EPSILON) ? SRD_EPSILON : p[i][2];
        layout->boxes[i].hd = (p[i][3] < SRD_EPSILON) ? SRD_EPSILON : p[i][3];
    }
}

/**
 * @brief Run a short gradient descent pass on the given layout using the critic.
 *
 * Uses simple SGD rather than L-BFGS to keep per-candidate cost low.
 * L-BFGS is reserved for the main continuous phase where the optimizer
 * object is constructed once and amortised over many iterations.
 *
 * @return Final loss after LocalOptimize.
 */
static float local_optimize(srd_sdf_layout_t *layout,
                            ferrum::srd::ISrdCritic *critic,
                            int n_steps) {
    torch::Tensor params, types;
    layout_to_tensors(layout, params, types);
    params = params.detach().requires_grad_(true);

    const float lr = 0.05f;
    float final_loss = 0.0f;

    for (int step = 0; step < n_steps; step++) {
        if (params.grad().defined()) params.grad().zero_();
        torch::Tensor loss = critic->score(params, types);
        loss.backward();
        final_loss = loss.item<float>();

        /* Manual SGD step */
        {
            torch::NoGradGuard no_grad;
            params -= lr * params.grad();
            /* Clamp hw, hd >= SRD_EPSILON */
            params.select(1, 2).clamp_min_(SRD_EPSILON);
            params.select(1, 3).clamp_min_(SRD_EPSILON);
        }
        params.requires_grad_(true);
    }

    /* Final loss after all steps */
    {
        torch::NoGradGuard no_grad;
        final_loss = critic->score(params, types).item<float>();
    }

    tensors_to_layout(layout, params);
    return final_loss;
}

/* ── Main function ───────────────────────────────────────────── */

extern "C" int srd_discrete_sample_candidates(
        const srd_sdf_layout_t *layout,
        const srd_descent_config_t *cfg,
        srd_candidate_t *out,
        int k,
        uint32_t *rng_state) {

    if (!layout || !cfg || !cfg->critic || !cfg->rules || !out || k <= 0)
        return 0;

    ferrum::srd::ISrdCritic *critic = cfg->critic->impl;

    /* Compute current loss on the unmodified layout */
    torch::Tensor cur_params, cur_types;
    layout_to_tensors(layout, cur_params, cur_types);
    float current_loss;
    {
        torch::NoGradGuard no_grad;
        current_loss = critic->score(cur_params, cur_types).item<float>();
    }

    /* Find applicable rules once */
    int applicable[SRD_MAX_RULES_TABLE];
    int n_applicable = srd_rule_find_applicable(
        cfg->rules, layout, applicable, SRD_MAX_RULES_TABLE, rng_state);

    for (int c = 0; c < k; c++) {
        out[c].delta_L = -INFINITY;
        out[c].rule_idx = -1;
        memset(&out[c].sel, 0, sizeof(out[c].sel));

        if (n_applicable == 0) continue;

        /* Pick a uniform random applicable rule */
        int pick = (int)(xorshift32(rng_state) % (uint32_t)n_applicable);
        int rule_idx = applicable[pick];
        out[c].rule_idx = rule_idx;

        /* Sample a valid selection */
        srd_selection_t sel;
        if (!srd_rule_sample_selection(cfg->rules, rule_idx, layout,
                                       &sel, rng_state)) {
            continue; /* no valid selection found */
        }
        out[c].sel = sel;

        /* Copy the layout */
        srd_sdf_layout_copy(&out[c].layout_copy, layout);

        /* Apply the rule to the copy */
        int new_indices[SRD_MAX_SELECT];
        int n_new = cfg->rules->rules[rule_idx].apply(
            &out[c].layout_copy, &sel, NULL, new_indices, SRD_MAX_SELECT);

        if (n_new < 0) continue; /* apply failed */

        /* Run LocalOptimize on the copy */
        float post_loss = local_optimize(
            &out[c].layout_copy, critic, cfg->local_optimize_steps);

        out[c].delta_L = current_loss - post_loss;
    }

    return k;
}

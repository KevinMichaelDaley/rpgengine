/**
 * @file srd_continuous_phase.cpp
 * @brief L-BFGS continuous optimisation phase for the SRD loop.
 *
 * Extracts layout box parameters into a libtorch tensor, runs L-BFGS
 * with the critic as the loss function, and writes back to the layout.
 *
 * Non-static functions (1): srd_continuous_phase_run
 */
#include "ferrum/procgen/srd/srd_continuous_phase.h"
#include "ferrum/procgen/srd/srd_critic.h"
#include "ferrum/procgen/srd/srd_room_type.h"

#include <torch/torch.h>
#include <algorithm>
#include <cstring>

/* ── srd_critic internal access ──────────────────────────────── */

struct srd_critic {
    ferrum::srd::ISrdCritic *impl;
};

/* ── Implementation ──────────────────────────────────────────── */

extern "C" float srd_continuous_phase_run(srd_sdf_layout_t *layout,
                                          const srd_descent_config_t *cfg) {
    if (!layout || !cfg || !cfg->critic || layout->n_boxes < 1)
        return -1.0f;

    const int n = layout->n_boxes;

    /* Extract parameters into a [N, 4] tensor: (cx, cz, hw, hd) */
    auto params = torch::zeros({n, 4}, torch::kFloat32);
    auto params_acc = params.accessor<float, 2>();
    for (int i = 0; i < n; i++) {
        params_acc[i][0] = layout->boxes[i].cx;
        params_acc[i][1] = layout->boxes[i].cz;
        params_acc[i][2] = layout->boxes[i].hw;
        params_acc[i][3] = layout->boxes[i].hd;
    }
    params = params.detach().requires_grad_(true);

    /* Build types tensor */
    auto types = torch::zeros({n}, torch::kInt64);
    auto types_acc = types.accessor<int64_t, 1>();
    for (int i = 0; i < n; i++) {
        types_acc[i] = static_cast<int64_t>(layout->boxes[i].type);
    }

    /* Create L-BFGS optimizer */
    auto lbfgs_opts = torch::optim::LBFGSOptions(/*lr=*/1.0)
        .max_iter(cfg->lbfgs_max_iter)
        .history_size(cfg->lbfgs_history_size)
        .tolerance_grad(cfg->lbfgs_tolerance_grad)
        .tolerance_change(cfg->lbfgs_tolerance_change);

    torch::optim::LBFGS optimizer({params}, lbfgs_opts);

    /* Get the critic implementation */
    ferrum::srd::ISrdCritic *critic = cfg->critic->impl;

    /* Run L-BFGS: single step call with closure */
    float final_loss = 0.0f;

    auto closure = [&]() -> torch::Tensor {
        optimizer.zero_grad();
        torch::Tensor loss = critic->score(params, types);
        loss.backward();
        return loss;
    };

    torch::Tensor loss_tensor = optimizer.step(closure);
    final_loss = loss_tensor.item<float>();

    /* Clamp hw, hd >= SRD_EPSILON before write-back */
    {
        torch::NoGradGuard no_grad;
        params.select(1, 2).clamp_min_(SRD_EPSILON);
        params.select(1, 3).clamp_min_(SRD_EPSILON);
    }

    /* Write parameters back to layout */
    auto out_acc = params.accessor<float, 2>();
    for (int i = 0; i < n; i++) {
        layout->boxes[i].cx = out_acc[i][0];
        layout->boxes[i].cz = out_acc[i][1];
        layout->boxes[i].hw = out_acc[i][2];
        layout->boxes[i].hd = out_acc[i][3];
    }

    return final_loss;
}

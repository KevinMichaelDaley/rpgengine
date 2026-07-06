/**
 * @file srd_critic_analytical.cpp
 * @brief AnalyticalCritic implementation and C API wrappers.
 *
 * All six loss terms are implemented using differentiable libtorch
 * operations so that the SRD optimiser can compute gradients via
 * autograd.  No raw C++ arithmetic is performed on tensor data.
 *
 * Loss terms:
 *   1. NonPenetration   — sum of pairwise axis-aligned overlap areas
 *   2. MinimumSize      — penalise boxes smaller than min_room_size
 *   3. TypeSeparation   — penalise boss/treasure adjacent to entrance
 *   4. AdjacencyCount   — per-type target degree deviation
 *   5. SoftReachability — power-iteration connectivity surrogate
 *   6. BoundsViolation  — penalise boxes outside layout boundary
 */

#include "ferrum/procgen/srd/srd_critic.h"
#include "ferrum/procgen/srd/srd_room_type.h"

#include <cstdlib>

/* ── srd_critic_t definition ──────────────────────────────────── */

struct srd_critic {
    ferrum::srd::ISrdCritic *impl;
};

/* ── Target adjacency degrees per room type ───────────────────── */

/**
 * @brief Desired number of neighbours (adjacency degree) per room type.
 *
 * Corridors want exactly 2 (pass-through), dead-ends want 1, boss
 * rooms want 2 (entrance + exit), generic rooms want 3, etc.
 */
static const float k_target_degree[SRD_ROOM_TYPE_COUNT] = {
    /* GENERIC   */ 3.0f,
    /* BAR       */ 2.0f,
    /* ENTRANCE  */ 2.0f,
    /* PRIVATE   */ 1.0f,
    /* STAIR_UP  */ 2.0f,
    /* STAIR_DOWN*/ 2.0f,
    /* CORRIDOR  */ 2.0f,
    /* DEAD_END  */ 1.0f,
    /* SECRET    */ 1.0f,
    /* BOSS      */ 2.0f,
    /* TREASURE  */ 1.0f,
};

namespace ferrum::srd {

/* ── Soft-adjacency matrix from SDF overlap ───────────────────── */

/**
 * @brief Build an [N, N] soft adjacency matrix from box parameters.
 *
 * Two boxes are "adjacent" when their signed distance is near zero.
 * We compute per-axis overlap via relu, multiply to get overlap area,
 * and pass through a sigmoid to produce a smooth 0-1 adjacency value.
 * A small gap tolerance (1.0 units) allows near-touching boxes to
 * register as adjacent.
 *
 * @param cx  [N] centre-x of each box.
 * @param cz  [N] centre-z of each box.
 * @param hw  [N] half-width of each box.
 * @param hd  [N] half-depth of each box.
 * @return    [N, N] soft adjacency matrix, values in (0, 1).
 */
static torch::Tensor build_soft_adjacency(torch::Tensor cx,
                                           torch::Tensor cz,
                                           torch::Tensor hw,
                                           torch::Tensor hd) {
    using namespace torch;
    const int64_t n = cx.size(0);

    /* Pairwise distances along each axis. */
    auto dx = (cx.unsqueeze(1) - cx.unsqueeze(0)).abs();   /* [N, N] */
    auto dz = (cz.unsqueeze(1) - cz.unsqueeze(0)).abs();   /* [N, N] */

    /* Sum of half-extents per pair. */
    auto sum_hw = hw.unsqueeze(1) + hw.unsqueeze(0);        /* [N, N] */
    auto sum_hd = hd.unsqueeze(1) + hd.unsqueeze(0);        /* [N, N] */

    /* Gap: positive means separated, negative means overlapping.
     * We add a tolerance so that boxes touching or nearly touching
     * still register as adjacent.                                    */
    const float gap_tol = 1.0f;
    auto gap_x = dx - sum_hw - gap_tol;
    auto gap_z = dz - sum_hd - gap_tol;

    /* Closeness: negative gap → large closeness, positive gap → ~0. */
    auto closeness_x = relu(-gap_x);
    auto closeness_z = relu(-gap_z);
    auto closeness   = closeness_x * closeness_z;

    /* Sigmoid to squash into (0, 1). Scale factor controls sharpness. */
    auto adj = torch::sigmoid(closeness * 2.0f - 3.0f);

    /* Zero out diagonal (a box is not adjacent to itself). */
    adj = adj * (1.0f - torch::eye(n, cx.options()));

    return adj;
}

/* ── Loss term helpers (all static) ───────────────────────────── */

/**
 * @brief NonPenetration loss: penalise pairwise box overlaps.
 */
static torch::Tensor loss_penetration(torch::Tensor cx,
                                       torch::Tensor cz,
                                       torch::Tensor hw,
                                       torch::Tensor hd) {
    using namespace torch;

    auto dx = (cx.unsqueeze(1) - cx.unsqueeze(0)).abs();
    auto dz = (cz.unsqueeze(1) - cz.unsqueeze(0)).abs();

    auto sum_hw = hw.unsqueeze(1) + hw.unsqueeze(0);
    auto sum_hd = hd.unsqueeze(1) + hd.unsqueeze(0);

    auto overlap_x = relu(sum_hw - dx);
    auto overlap_z = relu(sum_hd - dz);
    auto overlap   = overlap_x * overlap_z;

    /* Sum upper triangle only (avoid double-counting). */
    auto mask = torch::triu(torch::ones({cx.size(0), cx.size(0)},
                                         cx.options()), /*diagonal=*/1);
    return (overlap * mask).sum();
}

/**
 * @brief MinimumSize loss: penalise boxes smaller than required minimum.
 */
static torch::Tensor loss_min_size(torch::Tensor hw,
                                    torch::Tensor hd,
                                    torch::Tensor types,
                                    float min_room,
                                    float min_corr) {
    using namespace torch;

    /* Per-element minimum size based on type. */
    auto is_corridor = (types == static_cast<int64_t>(SRD_ROOM_CORRIDOR))
                            .to(hw.dtype());
    auto min_sz = is_corridor * min_corr + (1.0f - is_corridor) * min_room;

    auto pen_w = relu(min_sz - hw).square();
    auto pen_d = relu(min_sz - hd).square();
    return (pen_w + pen_d).sum();
}

/**
 * @brief TypeSeparation loss: penalise boss/treasure adjacent to entrance.
 */
static torch::Tensor loss_type_sep(torch::Tensor adj,
                                    torch::Tensor types) {
    using namespace torch;
    auto opts = adj.options();

    auto is_entrance = (types == static_cast<int64_t>(SRD_ROOM_ENTRANCE))
                            .to(opts);
    auto is_boss     = (types == static_cast<int64_t>(SRD_ROOM_BOSS))
                            .to(opts);
    auto is_treasure = (types == static_cast<int64_t>(SRD_ROOM_TREASURE))
                            .to(opts);
    auto is_danger   = is_boss + is_treasure;  /* [N] */

    /* Outer product: danger_i * entrance_j gives the mask of bad pairs. */
    auto bad_pairs = is_danger.unsqueeze(1) * is_entrance.unsqueeze(0);

    /* Symmetrise: also penalise entrance_i adjacent to danger_j. */
    bad_pairs = bad_pairs + bad_pairs.t();

    return (adj * bad_pairs).sum();
}

/**
 * @brief AdjacencyCount loss: penalise deviation from target degree.
 */
static torch::Tensor loss_adjacency_count(torch::Tensor adj,
                                           torch::Tensor types) {
    using namespace torch;

    const int64_t n = types.size(0);

    /* Build target degree vector from the per-type table. */
    auto target = torch::zeros({n}, adj.options());
    {
        auto target_table = torch::from_blob(
            const_cast<float *>(k_target_degree),
            {SRD_ROOM_TYPE_COUNT},
            torch::TensorOptions().dtype(torch::kFloat32));
        /* Gather target degrees by type index. */
        target = target_table.index({types.to(torch::kLong)}).to(adj.options());
    }

    /* Actual soft degree: row-sum of adjacency matrix. */
    auto degree = adj.sum(/*dim=*/1);

    return (degree - target).square().sum();
}

/**
 * @brief SoftReachability loss: power-iteration connectivity surrogate.
 *
 * Starting from a uniform distribution, iterate v = A * v (normalised)
 * 10 times. Penalise nodes with low reachability (low v).
 */
static torch::Tensor loss_reachability(torch::Tensor adj) {
    using namespace torch;

    const int64_t n = adj.size(0);
    if (n <= 1) {
        return torch::zeros({}, adj.options());
    }

    /* Row-normalise adjacency to create a transition matrix.
     * Add small epsilon to avoid division by zero.                  */
    auto row_sum = adj.sum(/*dim=*/1, /*keepdim=*/true) + 1e-6f;
    auto trans   = adj / row_sum;

    /* Start from uniform distribution. */
    auto v = torch::ones({n}, adj.options()) / static_cast<float>(n);

    /* Power iterate 10 times. */
    for (int i = 0; i < 10; ++i) {
        v = torch::mv(trans.t(), v);
        v = v / (v.sum() + 1e-8f);
    }

    /* Penalise low reachability: ideal is 1/N for all nodes.
     * If a node is unreachable its value drifts toward 0.           */
    auto ideal = torch::ones({n}, adj.options()) / static_cast<float>(n);
    return (v - ideal).square().sum() * static_cast<float>(n * n);
}

/**
 * @brief BoundsViolation loss: penalise boxes outside layout boundary.
 */
static torch::Tensor loss_bounds(torch::Tensor cx, torch::Tensor cz,
                                  torch::Tensor hw, torch::Tensor hd,
                                  float W, float H) {
    using namespace torch;

    /* Left   edge violation: box extends past x=0 */
    auto pen_left  = relu(-(cx - hw)).square();
    /* Right  edge violation: box extends past x=W */
    auto pen_right = relu(cx + hw - W).square();
    /* Top    edge violation: box extends past z=0 */
    auto pen_top   = relu(-(cz - hd)).square();
    /* Bottom edge violation: box extends past z=H */
    auto pen_bot   = relu(cz + hd - H).square();

    return (pen_left + pen_right + pen_top + pen_bot).sum();
}

/* ── AnalyticalCritic ─────────────────────────────────────────── */

AnalyticalCritic::AnalyticalCritic() : cfg_() {}
AnalyticalCritic::AnalyticalCritic(const Config &cfg) : cfg_(cfg) {}

torch::Tensor AnalyticalCritic::score(torch::Tensor params,
                                       torch::Tensor types) {
    /* Unpack columns: params is [N, 4] → (cx, cz, hw, hd). */
    auto cx = params.select(1, 0);
    auto cz = params.select(1, 1);
    auto hw = params.select(1, 2);
    auto hd = params.select(1, 3);

    /* Build soft adjacency matrix (shared by several terms). */
    auto adj = build_soft_adjacency(cx, cz, hw, hd);

    /* Accumulate weighted loss terms. */
    auto loss = torch::zeros({}, params.options());

    loss = loss + cfg_.w_penetration  * loss_penetration(cx, cz, hw, hd);
    loss = loss + cfg_.w_min_size     * loss_min_size(hw, hd, types,
                                                      cfg_.min_room_size,
                                                      cfg_.min_corridor_w);
    loss = loss + cfg_.w_separation   * loss_type_sep(adj, types);
    loss = loss + cfg_.w_adjacency    * loss_adjacency_count(adj, types);
    loss = loss + cfg_.w_reachability * loss_reachability(adj);
    loss = loss + cfg_.w_bounds       * loss_bounds(cx, cz, hw, hd,
                                                    cfg_.layout_w,
                                                    cfg_.layout_h);
    return loss;
}

} /* namespace ferrum::srd */

/* ── C API ────────────────────────────────────────────────────── */

extern "C" {

srd_critic_t *srd_critic_create_analytical(float layout_w, float layout_h) {
    auto *handle = static_cast<srd_critic_t *>(
        std::calloc(1, sizeof(srd_critic_t)));
    if (!handle) {
        return nullptr;
    }

    ferrum::srd::AnalyticalCritic::Config cfg;
    cfg.layout_w = layout_w;
    cfg.layout_h = layout_h;

    handle->impl = new (std::nothrow) ferrum::srd::AnalyticalCritic(cfg);
    if (!handle->impl) {
        std::free(handle);
        return nullptr;
    }
    return handle;
}

void srd_critic_destroy(srd_critic_t *c) {
    if (!c) {
        return;
    }
    delete c->impl;
    std::free(c);
}

} /* extern "C" */

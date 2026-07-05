/**
 * @file srd_loop.cpp
 * @brief SRD optimizer operating on the grammar symbol graph.
 *
 * Loss functions use graph diffusion kernels for differentiable
 * path-distance and line-of-sight metrics.  No rooms, corridors,
 * or SymX branches appear here — only linear algebra on the
 * soft adjacency graph.
 */
#include "ferrum/procgen/srd/srd_optimizer.h"
#include "ferrum/procgen/srd/srd_grammar.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>
#include <algorithm>

void srd_optimize_config_default(srd_optimize_config_t *cfg) {
    cfg->time_budget_s    = 5.0;
    cfg->max_steps        = 2000;
    cfg->rewrite_interval = 50;
    cfg->lr               = 0.5;
    cfg->min_improvement  = 0.99;
}

/* ────────────────────────────────────────────────────────────────
 *  Soft edge strength between two expanded symbol nodes.
 *
 *  margin = (half_a + half_b) - center_distance
 *  Positive margin → touching/touching → edge ≈ 1.
 *  Negative margin → separated → edge ≈ 0.
 *
 *  sigmoid makes this smooth and differentiable w.r.t. params.
 * ──────────────────────────────────────────────────────────────── */

static double soft_edge(const srd_tree_node_t *a,
                         const srd_tree_node_t *b,
                         double temperature) {
    double dx = a->params[SRD_PARAM_EXTENT_X]
              - b->params[SRD_PARAM_EXTENT_X];
    double dz = a->params[SRD_PARAM_EXTENT_Z]
              - b->params[SRD_PARAM_EXTENT_Z];
    double dist = sqrt(dx * dx + dz * dz);

    double half_sum = a->params[SRD_PARAM_EXTENT_X]
                    + b->params[SRD_PARAM_EXTENT_X]
                    + a->params[SRD_PARAM_EXTENT_Z]
                    + b->params[SRD_PARAM_EXTENT_Z];

    double margin = half_sum - dist;
    return 1.0 / (1.0 + exp(-margin / (temperature + 1e-6)));
}

/* ────────────────────────────────────────────────────────────────
 *  Build an N×N soft adjacency matrix from the grammar tree.
 *
 *  Only expanded terminal nodes are included.  The returned
 *  node_indices[] maps each matrix row back to the tree node.
 * ──────────────────────────────────────────────────────────────── */

static int build_soft_adjacency(
    const srd_grammar_t              *gram,
    double                            temperature,
    std::vector<std::vector<double>> &W,
    std::vector<int>                 &node_indices) {

    node_indices.clear();
    for (int i = 0; i < gram->n_nodes; i++) {
        if (gram->nodes[i].expanded) {
            node_indices.push_back(i);
        }
    }

    int N = (int)node_indices.size();
    W.assign(N, std::vector<double>(N, 0.0));

    for (int r = 0; r < N; r++) {
        for (int c = r + 1; c < N; c++) {
            double s = soft_edge(&gram->nodes[node_indices[r]],
                                  &gram->nodes[node_indices[c]],
                                  temperature);
            W[r][c] = s;
            W[c][r] = s;
        }
    }
    return N;
}

/* ────────────────────────────────────────────────────────────────
 *  Kernel 1: anisotropic (line-of-sight).
 *
 *  Reweights edges by alignment with the source→target direction.
 *  K[i][j] = W[i][j] * max(0, cos_angle_to_direction).
 * ──────────────────────────────────────────────────────────────── */

static void kernel_line_of_sight(
    const std::vector<std::vector<double>> &W,
    const srd_grammar_t                    *gram,
    const std::vector<int>                 &indices,
    int                                     src_idx,
    int                                     tgt_idx,
    std::vector<std::vector<double>>       &K) {

    int N = (int)W.size();
    K.assign(N, std::vector<double>(N, 0.0));

    const srd_tree_node_t *src = &gram->nodes[indices[src_idx]];
    const srd_tree_node_t *tgt = &gram->nodes[indices[tgt_idx]];

    double dx = tgt->params[SRD_PARAM_EXTENT_X]
              - src->params[SRD_PARAM_EXTENT_X];
    double dz = tgt->params[SRD_PARAM_EXTENT_Z]
              - src->params[SRD_PARAM_EXTENT_Z];
    double dir_norm = sqrt(dx * dx + dz * dz) + 1e-12;
    dx /= dir_norm;
    dz /= dir_norm;

    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            if (i == j || W[i][j] < 1e-6) continue;

            double ex = gram->nodes[indices[j]].params[SRD_PARAM_EXTENT_X]
                      - gram->nodes[indices[i]].params[SRD_PARAM_EXTENT_X];
            double ez = gram->nodes[indices[j]].params[SRD_PARAM_EXTENT_Z]
                      - gram->nodes[indices[i]].params[SRD_PARAM_EXTENT_Z];
            double en = sqrt(ex * ex + ez * ez) + 1e-12;

            /* Cosine alignment with source→target direction */
            double align = (ex * dx + ez * dz) / en;
            if (align < 0.0) align = 0.0;

            K[i][j] = W[i][j] * align;
        }
    }
}

/* ────────────────────────────────────────────────────────────────
 *  Kernel 2: inverse-distance (shortest-path proxy).
 *
 *  K[i][j] = W[i][j] / (euclidean_dist + eps)
 *  Closer nodes get higher transition probability.
 * ──────────────────────────────────────────────────────────────── */

static void kernel_inverse_distance(
    const std::vector<std::vector<double>> &W,
    const srd_grammar_t                    *gram,
    const std::vector<int>                 &indices,
    std::vector<std::vector<double>>       &K) {

    int N = (int)W.size();
    K.assign(N, std::vector<double>(N, 0.0));

    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            if (i == j || W[i][j] < 1e-6) continue;

            double dx = gram->nodes[indices[j]].params[SRD_PARAM_EXTENT_X]
                      - gram->nodes[indices[i]].params[SRD_PARAM_EXTENT_X];
            double dz = gram->nodes[indices[j]].params[SRD_PARAM_EXTENT_Z]
                      - gram->nodes[indices[i]].params[SRD_PARAM_EXTENT_Z];
            double dist = sqrt(dx * dx + dz * dz) + 1e-6;

            K[i][j] = W[i][j] / dist;
        }
    }
}

/* ────────────────────────────────────────────────────────────────
 *  Propagate a loss value through the graph to steady state.
 *
 *  Given a transition matrix P (row-normalized from kernel K),
 *  and boundary conditions L[anchors] = fixed, solve:
 *
 *    L[i] = Σ_j P[i][j] * L[j]   (harmonic function)
 *
 *  via Jacobi iteration.  Returns L[target] — the propagated
 *  loss value at the target node.
 *
 *  No branches — pure linear algebra.
 * ──────────────────────────────────────────────────────────────── */

static double propagate_loss(
    const std::vector<std::vector<double>> &K,
    const std::vector<double>              &boundary_values,
    const std::vector<int>                 &anchor_indices,
    int                                     max_iter) {

    int N = (int)K.size();
    if (N == 0) return 0.0;

    /* Build row-normalized transition matrix P */
    std::vector<double> row_sum(N, 0.0);
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            row_sum[i] += K[i][j];
        }
        row_sum[i] += 1e-12;  /* avoid division by zero */
    }

    /* Initialize L to 0.5 (neutral) */
    std::vector<double> L(N, 0.5);
    for (size_t ai = 0; ai < anchor_indices.size(); ai++) {
        L[anchor_indices[ai]] = boundary_values[ai];
    }

    /* Jacobi iteration */
    std::vector<double> L_new(N);
    for (int iter = 0; iter < max_iter; iter++) {
        for (int i = 0; i < N; i++) {
            L_new[i] = 0.0;
            for (int j = 0; j < N; j++) {
                L_new[i] += K[i][j] * L[j] / row_sum[i];
            }
        }

        /* Re-enforce boundary conditions */
        for (size_t ai = 0; ai < anchor_indices.size(); ai++) {
            L_new[anchor_indices[ai]] = boundary_values[ai];
        }

        /* Check convergence */
        double max_diff = 0.0;
        for (int i = 0; i < N; i++) {
            double d = fabs(L_new[i] - L[i]);
            if (d > max_diff) max_diff = d;
        }

        L.swap(L_new);
        if (max_diff < 1e-8) break;
    }

    /* Return the propagated value at the last anchor (target) */
    if (!anchor_indices.empty()) {
        return L[anchor_indices.back()];
    }
    return L[0];
}

/* ────────────────────────────────────────────────────────────────
 *  Total loss for the current grammar state.
 *
 *  For each explicit adjacency edge in the symbol grid, we apply
 *  both kernels and accumulate the propagated losses.
 *
 *  L = w_path * Σ path_loss(edge) + w_los * Σ los_loss(edge)
 * ──────────────────────────────────────────────────────────────── */

static double total_loss(const srd_grammar_t *gram,
                          const srd_grid_t   *grid,
                          double              temperature) {
    if (!gram || !grid) return 0.0;

    std::vector<std::vector<double>> W;
    std::vector<int> indices;
    int N = build_soft_adjacency(gram, temperature, W, indices);
    if (N < 2) return 0.0;

    double loss = 0.0;

    for (int e = 0; e < grid->n_edges; e++) {
        int ra = grid->edges[e].a;
        int rb = grid->edges[e].b;

        int ia = -1, ib = -1;
        for (int k = 0; k < N; k++) {
            if (gram->nodes[indices[k]].region_id == ra) ia = k;
            if (gram->nodes[indices[k]].region_id == rb) ib = k;
        }
        if (ia < 0 || ib < 0) continue;

        /* Reachability: weak edges → loss. Strong edges (doorways) → no loss. */
        double edge_strength = W[ia][ib];
        loss += (1.0 - edge_strength) * (1.0 - edge_strength);

        /* Line-of-sight: anisotropic propagation from ia → ib */
        {
            std::vector<std::vector<double>> K;
            kernel_line_of_sight(W, gram, indices, ia, ib, K);

            std::vector<int>    anchors = {ia, ib};
            std::vector<double> values  = {0.0, 1.0};
            double los = propagate_loss(K, values, anchors, 200);
            loss += (1.0 - los);
        }

        /* Path distance: inverse-distance propagation from ia → ib */
        {
            std::vector<std::vector<double>> K;
            kernel_inverse_distance(W, gram, indices, K);

            std::vector<int>    anchors = {ia, ib};
            std::vector<double> values  = {0.0, 1.0};
            double path = propagate_loss(K, values, anchors, 200);
            loss += (1.0 - path);
        }
    }

    return loss;
}

/* ────────────────────────────────────────────────────────────────
 *  Gradient of total_loss w.r.t. grammar node params.
 *
 *  Uses central finite differences because the loss propagation
 *  involves matrix row-normalization and Jacobi iteration which
 *  are all continuous, smooth operations.
 * ──────────────────────────────────────────────────────────────── */

static void gradient_step(srd_grammar_t *gram,
                           const srd_grid_t *grid,
                           double lr,
                           double temperature) {

    const double fd_eps = 1e-4;

    /* Collect all learnable params from expanded nodes */
    struct Slot { int node; int param; double value; };
    std::vector<Slot> params;

    for (int i = 0; i < gram->n_nodes; i++) {
        srd_tree_node_t *n = &gram->nodes[i];
        if (!n->expanded) continue;
        params.push_back({i, SRD_PARAM_EXTENT_X, (double)n->params[SRD_PARAM_EXTENT_X]});
        params.push_back({i, SRD_PARAM_EXTENT_Z, (double)n->params[SRD_PARAM_EXTENT_Z]});
    }

    double loss_before = total_loss(gram, grid, temperature);

    for (size_t pi = 0; pi < params.size(); pi++) {
        float orig = params[pi].value;

        /* +eps */
        gram->nodes[params[pi].node].params[params[pi].param] = orig + (float)fd_eps;
        double loss_plus = total_loss(gram, grid, temperature);

        /* -eps */
        gram->nodes[params[pi].node].params[params[pi].param] = orig - (float)fd_eps;
        double loss_minus = total_loss(gram, grid, temperature);

        /* Restore */
        gram->nodes[params[pi].node].params[params[pi].param] = orig;

        /* Gradient = (loss_plus - loss_minus) / (2*eps) */
        double grad = (loss_plus - loss_minus) / (2.0 * fd_eps);

        /* Gradient descent update */
        double new_val = orig - lr * grad;
        if (new_val < 1.0)  new_val = 1.0;
        if (new_val > 40.0) new_val = 40.0;

        gram->nodes[params[pi].node].params[params[pi].param] = (float)new_val;
    }

    (void)loss_before;
}

/* ────────────────────────────────────────────────────────────────
 *  Candidate rewrite evaluation + greedy max-cover.
 * ──────────────────────────────────────────────────────────────── */

struct Candidate {
    int    node_id;
    int    rule_id;
    double delta;
};

static std::vector<int> greedy_max_cover(
    std::vector<Candidate> &cands, double min_imp) {

    int N = (int)cands.size();
    if (N == 0) return {};

    /* Sort by delta descending */
    std::vector<int> order(N);
    for (int i = 0; i < N; i++) order[i] = i;
    std::sort(order.begin(), order.end(),
              [&](int a, int b) { return cands[a].delta > cands[b].delta; });

    std::vector<int>  selected;
    std::vector<bool> blocked(N, false);

    for (int oi = 0; oi < N; oi++) {
        int idx = order[oi];
        if (blocked[idx])          continue;
        if (cands[idx].delta <= min_imp) continue;

        selected.push_back(idx);

        /* Block all candidates on the same node */
        for (int j = 0; j < N; j++) {
            if (cands[j].node_id == cands[idx].node_id) {
                blocked[j] = true;
            }
        }
    }

    return selected;
}

/* ────────────────────────────────────────────────────────────────
 *  Main SRD loop:
 *    1. Continuous descent (gradient on graph loss)
 *    2. Stochastic sampling (candidate rewrites)
 *    3. Local evaluation (assess each rewrite)
 *    4. Greedy max-cover (resolve conflicts)
 *    5. Execution (apply accepted rewrites)
 * ──────────────────────────────────────────────────────────────── */

double srd_optimize(fr_room_box_t       *output_rooms,
                    uint32_t            *output_nr,
                    uint32_t             output_cap,
                    fr_corridor_seg_t  **output_corrs,
                    uint32_t            *output_nc,
                    const srd_loss_term_t *terms,
                    uint32_t              n_terms,
                    const srd_optimize_config_t *config,
                    const srd_grid_t           *grid,
                    const fr_room_graph_t      *graph) {

    (void)output_rooms; (void)output_nr; (void)output_cap;
    (void)output_corrs; (void)output_nc;
    (void)terms; (void)n_terms; (void)graph;

    *output_nc    = 0;
    *output_corrs = NULL;

    if (!grid || !config) return 0.0;

    const srd_rule_t       *rules   = srd_rule_table();
    int                     n_rules = srd_rule_count();
    time_t                  t0      = time(NULL);
    srd_optimize_config_t   cfg     = *config;
    double                  temperature = 1.0;

    /* ── Build initial grammar tree ────────────────────────── */
    srd_grammar_t gram;
    srd_grammar_init(&gram);

    for (int ri = 0; ri < grid->n_regions; ri++) {
        srd_symbol_t sym = srd_grid_region_symbol(grid, ri);
        if (sym == SRD_SYM_W || sym == SRD_SYM_DOT) continue;

        int nid = srd_grammar_add_node(&gram, sym, -1, -1);
        if (nid >= 0) {
            gram.nodes[nid].region_id = ri;
        }
    }

    /* ── Initial greedy expansion ──────────────────────────── */
    for (int i = 0; i < gram.n_nodes; i++) {
        srd_tree_node_t *n = &gram.nodes[i];
        if (n->expanded) continue;

        int matching[16];
        int nm = srd_rule_find_all(grid, n->region_id,
                                    rules, n_rules, matching, 16);
        if (nm > 0) {
            srd_rule_apply_to_node(&gram, i, &rules[matching[0]], grid);
        }
    }

    /* ── SRD main loop ─────────────────────────────────────── */
    int step = 0;

    while (step < cfg.max_steps) {
        double elapsed = difftime(time(NULL), t0);
        if (elapsed >= cfg.time_budget_s) break;

        /* Step 1: Continuous descent */
        gradient_step(&gram, grid, cfg.lr, temperature);
        temperature *= 0.998;
        if (temperature < 0.01) temperature = 0.01;
        step++;

        if (step % cfg.rewrite_interval != 0) continue;

        /* Steps 2-3: Stochastic sampling + local evaluation */
        std::vector<Candidate> candidates;

        for (int i = 0; i < gram.n_nodes && candidates.size() < 32; i++) {
            srd_tree_node_t *n = &gram.nodes[i];
            if (n->expanded) continue;

            int matching[16];
            int nm = srd_rule_find_all(grid, n->region_id,
                                        rules, n_rules, matching, 16);
            if (nm < 1) continue;

            /* Stochastic: sample up to 3 */
            if (nm > 3) {
                int kept = 0;
                while (kept < 3 && nm > 0) {
                    int pick = rand() % nm;
                    matching[kept] = matching[pick];
                    matching[pick] = matching[--nm];
                    kept++;
                }
                nm = kept;
            }

            for (int mi = 0; mi < nm; mi++) {
                srd_grammar_t trial;
                memcpy(&trial, &gram, sizeof(gram));
                srd_rule_apply_to_node(&trial, i, &rules[matching[mi]], grid);

                double before = total_loss(&gram,  grid, temperature);
                double after  = total_loss(&trial, grid, temperature);

                candidates.push_back({ i, matching[mi], before - after });
            }
        }

        /* Step 4: Greedy max-cover */
        std::vector<int> accepted = greedy_max_cover(candidates,
                                                      cfg.min_improvement);

        /* Step 5: Execution */
        for (int ai : accepted) {
            Candidate &c = candidates[ai];
            srd_rule_apply_to_node(&gram, c.node_id, &rules[c.rule_id], grid);
        }
    }

    /* ── Extract final geometry ────────────────────────────── */
    fr_room_box_t     *final_rooms  = NULL;
    uint32_t           final_nr     = 0;
    fr_corridor_seg_t *final_corrs  = NULL;
    uint32_t           final_nc     = 0;

    srd_grammar_extract(&gram, &final_rooms, &final_nr,
                         &final_corrs, &final_nc);

    if (final_nr > 0 && final_nr <= output_cap) {
        memcpy(output_rooms, final_rooms, final_nr * sizeof(fr_room_box_t));
        *output_nr = final_nr;
    }
    *output_nc    = final_nc;
    *output_corrs = final_corrs;
    free(final_rooms);

    return total_loss(&gram, grid, temperature);
}

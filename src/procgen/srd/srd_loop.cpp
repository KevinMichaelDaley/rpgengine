/**
 * @file srd_loop.cpp
 * @brief SRD optimizer operating on the grammar symbol graph.
 */
#include "ferrum/procgen/srd/srd_energy.h"
#include "ferrum/procgen/srd/srd_grammar.h"
#include "ferrum/procgen/srd/srd_loss_primitives.h"
#include "ferrum/procgen/srd/srd_optimizer.h"

#include <symx>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

void srd_optimize_config_default(srd_optimize_config_t *cfg) {
  cfg->time_budget_s = 5.0;
  cfg->max_steps = 2000;
  cfg->rewrite_interval = 50;
  cfg->lr = 0.5;
  cfg->min_improvement = 0.99;
  cfg->candidate_budget = 8;
  cfg->verbose = 0;
}

static const char *srd_action_name(srd_action_t a) {
  switch (a) {
  case SRD_ACT_ROOM:
    return "ROOM";
  case SRD_ACT_CONNECT:
    return "CON";
  case SRD_ACT_DISCONNECT:
    return "DCON";
  case SRD_ACT_MERGE_ROOM:
    return "MRG_R";
  case SRD_ACT_TO_DOOR:
    return "TO_D";
  case SRD_ACT_TO_STAIR:
    return "TO_S";
  case SRD_ACT_EMIT_WALL:
    return "WALL";
  case SRD_ACT_EMIT_VOID:
    return "VOID";
  case SRD_ACT_EMIT_FLOOR:
    return "FLR";
  case SRD_ACT_EMIT_CEILING:
    return "CEIL";
  case SRD_ACT_EMIT_STAIR:
    return "STAR";
  case SRD_ACT_CORRIDOR:
    return "COR";
  case SRD_ACT_MERGE_CORRIDOR:
    return "MCOR";
  default:
    return "?";
  }
}

static const char *srd_symbol_name(srd_symbol_t s) {
  switch (s) {
  case SRD_SYM_R:
    return "R";
  case SRD_SYM_B:
    return "B";
  case SRD_SYM_G:
    return "G";
  case SRD_SYM_P:
    return "P";
  case SRD_SYM_W:
    return "W";
  case SRD_SYM_DOT:
    return ".";
  case SRD_SYM_UP:
    return "^";
  case SRD_SYM_DN:
    return "v";
  case SRD_SYM_WALL_SEG:
    return "WS";
  case SRD_SYM_FLOOR_SEG:
    return "FS";
  case SRD_SYM_CEILING_SEG:
    return "CS";
  case SRD_SYM_CORRIDOR:
    return "COR";
  case SRD_SYM_STAIR_ANCHOR:
    return "SA";
  case SRD_SYM_WALL_WITH_DOOR:
    return "WD";
  case SRD_SYM_WALL:
    return "w";
  case SRD_SYM_VOID:
    return "o";
  case SRD_SYM_FLOOR:
    return "f";
  case SRD_SYM_CEILING:
    return "c";
  case SRD_SYM_STAIR:
    return "s";
  default:
    return "?";
  }
}

/* ── soft_edge ──────────────────────────────────────────────── */

static double soft_edge(const srd_tree_node_t *a, const srd_tree_node_t *b,
                        double temperature) {
  double dx = a->params[SRD_PARAM_EXTENT_X] - b->params[SRD_PARAM_EXTENT_X];
  double dz = a->params[SRD_PARAM_EXTENT_Z] - b->params[SRD_PARAM_EXTENT_Z];
  double dist = sqrt(dx * dx + dz * dz);
  double half_sum =
      a->params[SRD_PARAM_EXTENT_X] + b->params[SRD_PARAM_EXTENT_X] +
      a->params[SRD_PARAM_EXTENT_Z] + b->params[SRD_PARAM_EXTENT_Z];
  double margin = half_sum - dist;
  double spatial = 1.0 / (1.0 + exp(-margin / (temperature + 1e-6)));
  if (a->connect_to == b->id || b->connect_to == a->id)
    spatial = 0.5 + 0.5 * spatial;
  return spatial;
}

static int build_soft_adjacency(const srd_grammar_t *gram, double temperature,
                                std::vector<std::vector<double>> &W,
                                std::vector<int> &node_indices) {
  node_indices.clear();
  for (int i = 0; i < gram->n_nodes; i++) {
    const srd_tree_node_t *n = &gram->nodes[i];
    /* Terminal, unexpanded region, or intermediate with position (parent
     * expanded) */
    if (srd_symbol_is_terminal(n->symbol))
      node_indices.push_back(i);
    else if (n->region_id >= 0 && n->n_children == 0)
      node_indices.push_back(i);
    else if (n->n_children == 0 && n->parent_id >= 0 &&
             n->params[SRD_PARAM_EXTENT_X] != 0.0f)
      node_indices.push_back(i);
  }
  int N = (int)node_indices.size();
  W.assign(N, std::vector<double>(N, 0.0));
  for (int r = 0; r < N; r++)
    for (int c = r + 1; c < N; c++) {
      double s = soft_edge(&gram->nodes[node_indices[r]],
                           &gram->nodes[node_indices[c]], temperature);
      W[r][c] = s;
      W[c][r] = s;
    }
  return N;
}

static void kernel_line_of_sight(const std::vector<std::vector<double>> &W,
                                 const srd_grammar_t *gram,
                                 const std::vector<int> &indices, int src_idx,
                                 int tgt_idx,
                                 std::vector<std::vector<double>> &K) {
  int N = (int)W.size();
  K.assign(N, std::vector<double>(N, 0.0));
  const srd_tree_node_t *src = &gram->nodes[indices[src_idx]];
  const srd_tree_node_t *tgt = &gram->nodes[indices[tgt_idx]];
  double dx = tgt->params[SRD_PARAM_EXTENT_X] - src->params[SRD_PARAM_EXTENT_X];
  double dz = tgt->params[SRD_PARAM_EXTENT_Z] - src->params[SRD_PARAM_EXTENT_Z];
  double dn = sqrt(dx * dx + dz * dz) + 1e-12;
  dx /= dn;
  dz /= dn;
  for (int i = 0; i < N; i++)
    for (int j = 0; j < N; j++) {
      if (i == j || W[i][j] < 1e-6)
        continue;
      double ex =
          gram->nodes[indices[j]].params[0] - gram->nodes[indices[i]].params[0];
      double ez =
          gram->nodes[indices[j]].params[3] - gram->nodes[indices[i]].params[3];
      double en = sqrt(ex * ex + ez * ez) + 1e-12;
      double a = (ex * dx + ez * dz) / en;
      if (a < 0)
        a = 0;
      K[i][j] = W[i][j] * a;
    }
}

static void kernel_inverse_distance(const std::vector<std::vector<double>> &W,
                                    const srd_grammar_t *gram,
                                    const std::vector<int> &indices,
                                    std::vector<std::vector<double>> &K) {
  int N = (int)W.size();
  K.assign(N, std::vector<double>(N, 0.0));
  for (int i = 0; i < N; i++)
    for (int j = 0; j < N; j++) {
      if (i == j || W[i][j] < 1e-6)
        continue;
      double dx =
          gram->nodes[indices[j]].params[0] - gram->nodes[indices[i]].params[0];
      double dz =
          gram->nodes[indices[j]].params[3] - gram->nodes[indices[i]].params[3];
      K[i][j] = W[i][j] / (sqrt(dx * dx + dz * dz) + 1e-6);
    }
}

static double total_loss(const srd_grammar_t *gram, const srd_grid_t *grid,
                         double temperature, const srd_loss_term_t *terms,
                         uint32_t n_terms) {
  if (!gram || !grid)
    return 0.0;
  std::vector<std::vector<double>> W;
  std::vector<int> indices;
  int N = build_soft_adjacency(gram, temperature, W, indices);
  if (N < 2)
    return 0.0;
  double loss = 0.0;

  for (uint32_t ti = 0; ti < n_terms && terms; ti++) {
    const srd_loss_term_t *t = &terms[ti];
    if (t->primitive == FR_LOSS_NON_PENETRATION && t->all_rooms) {
      for (int i = 0; i < N; i++) {
        for (int j = i + 1; j < N; j++) {
          if (gram->nodes[indices[i]].region_id < 0)
            continue;
          if (gram->nodes[indices[j]].region_id < 0)
            continue;
          double dx = gram->nodes[indices[i]].params[0] -
                      gram->nodes[indices[j]].params[0];
          double dz = gram->nodes[indices[i]].params[3] -
                      gram->nodes[indices[j]].params[3];
          loss += exp(-(dx * dx + dz * dz) / 16.0);
        }
      }
      continue;
    }
    if (t->primitive == FR_LOSS_MINIMUM_SIZE && t->all_rooms) {
      for (int i = 0; i < N; i++) {
        if (gram->nodes[indices[i]].region_id < 0)
          continue;
        double mx = fabs(gram->nodes[indices[i]].params[0]);
        double mz = fabs(gram->nodes[indices[i]].params[3]);
        double ms = (double)t->target_value;
        if (mx < ms)
          loss += (ms - mx) * (ms - mx);
        if (mz < ms)
          loss += (ms - mz) * (ms - mz);
      }
      continue;
    }
    int na = -1, nb = -1;
    for (int i = 0; i < N && (na < 0 || nb < 0); i++) {
      int rid = gram->nodes[indices[i]].region_id;
      if (rid < 0 || rid >= (int)grid->n_regions)
        continue;
      for (uint32_t li = 0; li < SRD_MAX_LABELS; li++) {
        uint32_t lab = t->label_indices[li];
        if (lab == (uint32_t)-1 || lab >= grid->n_regions)
          continue;
        if (rid == (int)lab) {
          if (na < 0)
            na = i;
          else
            nb = i;
        }
      }
    }
    if (na < 0 || nb < 0)
      continue;
    if (t->primitive == FR_LOSS_PATH_DISTANCE) {
      std::vector<std::vector<double>> Kp;
      kernel_inverse_distance(W, gram, indices, Kp);
      for (int i = 0; i < N; i++) {
        double rs = 1e-12;
        for (int j = 0; j < N; j++)
          rs += Kp[i][j];
        for (int j = 0; j < N; j++)
          Kp[i][j] /= rs;
      }
      std::vector<double> L(N, 0.0);
      for (int iter = 0; iter < 500; iter++)
        for (int i = 0; i < N; i++) {
          if (i == na)
            continue;
          double s = 0;
          for (int j = 0; j < N; j++)
            s += Kp[i][j] * L[j];
          L[i] = s;
        }
      double d = L[nb];
      if (d > 1e10)
        d = 1e6;
      double err = d - (double)t->target_value;
      loss += err * err * 0.001;
    }
    if (t->primitive == FR_LOSS_LINE_OF_SIGHT) {
      std::vector<std::vector<double>> Kl;
      kernel_line_of_sight(W, gram, indices, na, nb, Kl);
      std::vector<double> L(N, 0.0);
      L[na] = 0;
      L[nb] = 1;
      for (int iter = 0; iter < 500; iter++) {
        std::vector<double> Ln(N, 0.0);
        for (int i = 0; i < N; i++) {
          if (i == na || i == nb) {
            Ln[i] = L[i];
            continue;
          }
          double rs = 1e-12;
          for (int j = 0; j < N; j++)
            rs += Kl[i][j];
          double s = 0;
          for (int j = 0; j < N; j++)
            s += Kl[i][j] * L[j] / rs;
          Ln[i] = s;
        }
        L.swap(Ln);
      }
      loss += (1.0 - L[na] + L[nb]) * 0.05;
    }
  }
  /* ── Anchor: penalize region nodes deviating from grid seed ── */
  if (n_terms == 0) {
    for (int e = 0; e < grid->n_edges; e++) {
      int ia = -1, ib = -1;
      for (int k = 0; k < N; k++) {
        if (gram->nodes[indices[k]].region_id == grid->edges[e].a)
          ia = k;
        if (gram->nodes[indices[k]].region_id == grid->edges[e].b)
          ib = k;
      }
      if (ia < 0 || ib < 0)
        continue;
      double s = W[ia][ib];
      loss += (1.0 - s) * (1.0 - s);
    }
  }
  return loss;
}

static void gradient_step(srd_grammar_t *gram, const srd_grid_t *grid,
                          double lr, double temperature,
                          const srd_loss_term_t *terms, uint32_t n_terms) {

  /* ── SymX: per-node size energy ─────────────────────────── */
  for (uint32_t ti = 0; ti < n_terms && terms; ti++) {
    if (terms[ti].primitive != FR_LOSS_MINIMUM_SIZE)
      continue;
    double ms = (double)terms[ti].target_value;
    for (int i = 0; i < gram->n_nodes; i++) {
      if (gram->nodes[i].region_id < 0)
        continue;
      if (!terms[ti].all_rooms)
        continue;

      double cx = gram->nodes[i].params[SRD_PARAM_EXTENT_X];
      double cz = gram->nodes[i].params[SRD_PARAM_EXTENT_Z];
      symx::Workspace ws;
      auto vx = ws.make_scalar(), vz = ws.make_scalar();
      vx.set_value(cx);
      vz.set_value(cz);
      symx::Scalar E =
          (symx::max(ws.make_scalar().make_constant(ms) - vx, vx.get_zero())) *
              (symx::max(ws.make_scalar().make_constant(ms) - vx,
                         vx.get_zero())) +
          (symx::max(ws.make_scalar().make_constant(ms) - vz, vz.get_zero())) *
              (symx::max(ws.make_scalar().make_constant(ms) - vz,
                         vz.get_zero()));
      auto vg = symx::value_gradient(E, {vx, vz});
      symx::Compiled<double> c(vg, "min_size_grad", "/tmp/symx_grad");
      c.set(vx, cx);
      c.set(vz, cz);
      symx::View<double> r = c.run();
      gram->nodes[i].params[0] -= (float)(lr * r[1]);
      gram->nodes[i].params[3] -= (float)(lr * r[2]);
    }
  }

  /* ── SymX: pairwise non-penetration ─────────────────────── */
  std::vector<std::vector<double>> W;
  std::vector<int> indices;
  int N = build_soft_adjacency(gram, temperature, W, indices);
  for (uint32_t ti = 0; ti < n_terms && terms; ti++) {
    if (terms[ti].primitive != FR_LOSS_NON_PENETRATION || !terms[ti].all_rooms)
      continue;
    for (int i = 0; i < N; i++) {
      for (int j = i + 1; j < N; j++) {
        if (gram->nodes[indices[i]].region_id < 0)
          continue;
        if (gram->nodes[indices[j]].region_id < 0)
          continue;
        double ax = gram->nodes[indices[i]].params[0];
        double az = gram->nodes[indices[i]].params[3];
        double bx = gram->nodes[indices[j]].params[0];
        double bz = gram->nodes[indices[j]].params[3];

        symx::Workspace ws;
        auto vax = ws.make_scalar(), vaz = ws.make_scalar();
        auto vbx = ws.make_scalar(), vbz = ws.make_scalar();
        auto vh = ws.make_scalar();
        vh.set_value(4.0);
        symx::Scalar E = srd::srd_overlap_energy(ws, vax, vaz, vh, vh, vh, vbx,
                                                 vbz, vh, vh, vh, temperature);
        auto vg = symx::value_gradient(E, {vax, vaz, vbx, vbz});
        symx::Compiled<double> c(vg, "overlap_grad", "/tmp/symx_grad");
        c.set(vax, ax);
        c.set(vaz, az);
        c.set(vbx, bx);
        c.set(vbz, bz);
        symx::View<double> r = c.run();
        gram->nodes[indices[i]].params[0] -= (float)(lr * r[1]);
        gram->nodes[indices[i]].params[3] -= (float)(lr * r[2]);
        gram->nodes[indices[j]].params[0] -= (float)(lr * r[3]);
        gram->nodes[indices[j]].params[3] -= (float)(lr * r[4]);
      }
    }
  }

  /* ── Finite diff for path/los terms (fallback) ──────────── */
  const double eps = 1e-3;
  for (int i = 0; i < gram->n_nodes; i++) {
    srd_tree_node_t *n = &gram->nodes[i];
    if (!(n->n_children > 0 || srd_symbol_is_terminal(n->symbol) ||
          n->region_id >= 0))
      continue;
    for (int p = 0; p < 2; p++) {
      int pid = (p == 0) ? SRD_PARAM_EXTENT_X : SRD_PARAM_EXTENT_Z;
      float orig = n->params[pid];
      n->params[pid] = orig + (float)eps;
      double lp = total_loss(gram, grid, temperature, terms, n_terms);
      n->params[pid] = orig - (float)eps;
      double lm = total_loss(gram, grid, temperature, terms, n_terms);
      n->params[pid] = orig;
      double g = (lp - lm) / (2.0 * eps);
      double nv = orig - lr * g;
      if (nv < 1.0)
        nv = 1.0;
      if (nv > 40.0)
        nv = 40.0;
      n->params[pid] = (float)nv;
    }
  }
  for (int i = 0; i < gram->n_nodes; i++) {
    int rid = gram->nodes[i].region_id;
    if (rid >= 0 && rid < (int)grid->n_regions) {
      double sx = (double)(grid->regions[rid].first_x * 2 + 1);
      double sz = (double)(grid->regions[rid].first_z * 2 + 1);
      if (gram->nodes[i].params[0] < sx - 4.0f)
        gram->nodes[i].params[0] = (float)(sx - 4.0);
      if (gram->nodes[i].params[0] > sx + 4.0f)
        gram->nodes[i].params[0] = (float)(sx + 4.0);
      if (gram->nodes[i].params[3] < sz - 4.0f)
        gram->nodes[i].params[3] = (float)(sz - 4.0);
      if (gram->nodes[i].params[3] > sz + 4.0f)
        gram->nodes[i].params[3] = (float)(sz + 4.0);
    }
  }
}

double srd_optimize(fr_room_box_t *rooms, uint32_t *n_rooms, uint32_t cap,
                    fr_corridor_seg_t **corridors, uint32_t *n_corridors,
                    const srd_loss_term_t *terms, uint32_t n_terms,
                    const srd_optimize_config_t *config, const srd_grid_t *grid,
                    srd_grammar_t *gram) {
  (void)rooms;
  (void)n_rooms;
  (void)cap;
  *n_corridors = 0;
  *corridors = NULL;
  if (!grid || !config || !gram)
    return 0.0;
  const srd_rule_t *rules = srd_rule_table();
  int n_rules = srd_rule_count();
  time_t t0 = time(NULL);
  srd_optimize_config_t cfg = *config;
  double temperature = 1.0;

  for (int i = 0; i < gram->n_nodes; i++) {
    srd_tree_node_t *n = &gram->nodes[i];
    if (n->region_id >= 0 && n->region_id < (int)grid->n_regions) {
      n->params[0] = (float)(grid->regions[n->region_id].first_x * 2 + 1);
      n->params[3] = (float)(grid->regions[n->region_id].first_z * 2 + 1);
    } else {
      n->params[0] = 2.0f + (rand() % 20);
      n->params[3] = 2.0f + (rand() % 20);
    }
  }

  int step = 0;
  int srd_epoch = 0;
  while (step < cfg.max_steps) {
    if (difftime(time(NULL), t0) >= cfg.time_budget_s)
      break;
    for (int gi = 0; gi < 50 && step < cfg.max_steps; gi++, step++) {
      gradient_step(gram, grid, cfg.lr, temperature, terms, n_terms);
      temperature *= 0.998;
      if (temperature < 0.01)
        temperature = 0.01;
    }
    srd_epoch++;
    double current_loss = total_loss(gram, grid, temperature, terms, n_terms);
    if (cfg.verbose)
      printf("[srd] loss=%.3f step=%d nodes=%d\n", current_loss, step,
             gram->n_nodes);

    bool converged = (current_loss < 0.03);

    /* Stochastic rewrite: weighted random node, random rule, sandbox eval */
    std::vector<int> cands;
    std::vector<double> cands_weight;
    for (int i = 0; i < gram->n_nodes; i++) {
      srd_tree_node_t *n = &gram->nodes[i];
      if (srd_symbol_is_terminal(n->symbol))
        continue;
      if (n->n_children > 0)
        continue;

      /* Compute tree depth from root */
      int depth = 0;
      int p = n->parent_id;
      while (p >= 0 && depth < 100) {
        p = gram->nodes[p].parent_id;
        depth++;
      }

      double w = 1.0;
      bool is_intermediate =
          (n->symbol >= SRD_SYM_WALL_SEG && n->symbol <= SRD_SYM_STAIR_ANCHOR);
      if (is_intermediate) {
        /* Bias: intermediates get higher weight as epoch and depth grow */
        int age = srd_epoch - n->created_step;
        w = 0.1 + 0.1 * (double)age + 0.05 * (double)depth;
        if (w > 1.0)
          w = 1.0;
      }
      cands.push_back(i);
      cands_weight.push_back(w);
    }

    /* Weighted sampling: higher weight = more likely to pick */
    double total_w = 0.0;
    for (auto w : cands_weight)
      total_w += w;
    if (total_w < 1e-12)
      continue;
    double rw = (double)rand() / RAND_MAX * total_w;
    double acc = 0.0;
    int pick = 0;
    for (size_t ci = 0; ci < cands.size(); ci++) {
      acc += cands_weight[ci];
      if (acc >= rw) {
        pick = ci;
        break;
      }
    }
    int nid = cands[pick];
    int matching[16];
    int nm = srd_rule_find_all(grid, &gram->nodes[nid], rules, n_rules,
                               matching, 16);
    if (nm == 0)
      continue;
    int rid = matching[rand() % nm];
    const srd_rule_t *r = &rules[rid];

    srd_grammar_t *sb = (srd_grammar_t *)malloc(sizeof(*sb));
    if (!sb)
      continue;
    memcpy(sb, gram, sizeof(*sb));
    int old_n = sb->n_nodes;
    srd_rule_apply_to_node(sb, nid, r, grid);
    int new_n = sb->n_nodes;
    for (int ni = old_n; ni < new_n; ni++) {
      int pid = sb->nodes[ni].parent_id;
      if (pid >= 0 && pid < old_n) {
        sb->nodes[ni].params[0] =
            sb->nodes[pid].params[0] + ((float)(rand() % 1000) - 500) * 1e-6f;
        sb->nodes[ni].params[3] =
            sb->nodes[pid].params[3] + ((float)(rand() % 1000) - 500) * 1e-6f;
      }
    }
    double sb_loss = total_loss(sb, grid, temperature, terms, n_terms);
    double delta = current_loss - sb_loss;
    if (fabs(delta) > 10 && cfg.verbose)
      printf("[srd] dbg: rule=%s:%s current=%.3f sb=%.3f delta=%.2g\n",
             srd_symbol_name(r->symbol), srd_action_name(r->action),
             current_loss, sb_loss, delta);
    free(sb);
    if (delta > 0 || (current_loss < 0.03 &&
                       r->action >= SRD_ACT_EMIT_WALL &&
                       r->action <= SRD_ACT_EMIT_STAIR)) {
      if (cfg.verbose && delta <= 0) {
      }
      old_n = gram->n_nodes;
      srd_rule_apply_to_node(gram, nid, r, grid);
      for (int ni = old_n; ni < gram->n_nodes; ni++) {
        gram->nodes[ni].created_step = srd_epoch;
        int pid = gram->nodes[ni].parent_id;
        if (pid >= 0 && pid < old_n) {
          gram->nodes[ni].params[0] = gram->nodes[pid].params[0] +
                                      ((float)(rand() % 1000) - 500) * 1e-6f;
          gram->nodes[ni].params[3] = gram->nodes[pid].params[3] +
                                      ((float)(rand() % 1000) - 500) * 1e-6f;
        }
      }
    }
    if (cfg.verbose)
      printf("[srd] %s:%s Δ=%.2g\n", srd_symbol_name(r->symbol),
             srd_action_name(r->action), delta);

    for (int ci = 0; ci < gram->n_nodes; ci++) {
      srd_tree_node_t *cn = &gram->nodes[ci];
      int rid = cn->region_id;
      if (rid >= 0 && rid < (int)grid->n_regions) {
        double sx = (double)(grid->regions[rid].first_x * 2 + 1);
        double sz = (double)(grid->regions[rid].first_z * 2 + 1);
        if (cn->params[0] < sx - 4.0)
          cn->params[0] = (float)(sx - 4.0);
        if (cn->params[0] > sx + 4.0)
          cn->params[0] = (float)(sx + 4.0);
        if (cn->params[3] < sz - 4.0)
          cn->params[3] = (float)(sz - 4.0);
        if (cn->params[3] > sz + 4.0)
          cn->params[3] = (float)(sz + 4.0);
      }
      if (cn->params[0] < 1.0f)
        cn->params[0] = 1.0f;
      if (cn->params[3] < 1.0f)
        cn->params[3] = 1.0f;
      if (cn->params[0] > 40.0f)
        cn->params[0] = 40.0f;
      if (cn->params[3] > 40.0f)
        cn->params[3] = 40.0f;
    }
  }
  return 0.0;
}

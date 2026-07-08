#include "ferrum/procgen/srd/srd_optimizer.h"
#include "ferrum/procgen/srd/srd_grammar_impl.h"
#include "ferrum/procgen/srd/srd_symbol_grid.h"
#include "ferrum/procgen/srd/srd_rules.h"
#include "ferrum/procgen/srd/srd_energy.h"
#include "ferrum/procgen/procgen_srd_grammar.h"

#include <symx>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>

using srd::srd_room_sdf_energy;
using srd::srd_overlap_energy;

void srd_optimize_config_default(srd_optimize_config_t *cfg) {
    cfg->time_budget_s    = 5.0;
    cfg->max_steps        = 2000;
    cfg->rewrite_interval = 50;
    cfg->lr               = 0.5;
    cfg->min_improvement  = 0.99;
}

/* ── Extract room params → SymX var vector ──────────────────── */

static void room_gradient_symx(fr_room_box_t *room,
                                double *gx, double *gz,
                                double *ghx, double *ghz) {
    symx::Workspace ws;
    auto cx = ws.make_scalar(), cz = ws.make_scalar();
    auto hx = ws.make_scalar(), hz = ws.make_scalar();

    double hy = (room->ceil_z - room->floor_z) * 0.5;
    auto hy_var = ws.make_scalar();
    hy_var.set_value(hy);

    symx::Scalar E = srd_room_sdf_energy(ws, cx, cz, hx, hy_var, hz, 0.5);

    std::vector<symx::Scalar> wrt = { cx, cz, hx, hz };
    std::vector<symx::Scalar> vg = symx::value_gradient(E, wrt);

    symx::Compiled<double> c(vg, "opt_grad", "../codegen_srd");
    c.set(cx, (double)room->center_x);
    c.set(cz, (double)room->center_z);
    c.set(hx, (double)room->half_extent_x);
    c.set(hz, (double)room->half_extent_z);

    symx::View<double> r = c.run();
    *gx  = r[1];   /* dE/dcx */
    *gz  = r[2];   /* dE/dcz */
    *ghx = r[3];   /* dE/dhx */
    *ghz = r[4];   /* dE/dhz */
    (void)r[0];    /* energy value */
}

/* ── Overlap energy for two rooms ────────────────────────────── */

static double overlap_value(fr_room_box_t *a, fr_room_box_t *b) {
    symx::Workspace ws;
    auto acx = ws.make_scalar(), acz = ws.make_scalar();
    auto ahx = ws.make_scalar(), ahz = ws.make_scalar();
    auto bcx = ws.make_scalar(), bcz = ws.make_scalar();
    auto bhx = ws.make_scalar(), bhz = ws.make_scalar();

    double ahy = (a->ceil_z - a->floor_z) * 0.5;
    double bhy = (b->ceil_z - b->floor_z) * 0.5;
    auto ahy_var = ws.make_scalar(); ahy_var.set_value(ahy);
    auto bhy_var = ws.make_scalar(); bhy_var.set_value(bhy);

    symx::Scalar E = srd_overlap_energy(ws,
        acx, acz, ahx, ahy_var, ahz,
        bcx, bcz, bhx, bhy_var, bhz, 0.5);

    symx::Compiled<double> c({ E }, "ovlp", "../codegen_srd");
    c.set(acx, (double)a->center_x); c.set(acz, (double)a->center_z);
    c.set(ahx, (double)a->half_extent_x); c.set(ahz, (double)a->half_extent_z);
    c.set(bcx, (double)b->center_x); c.set(bcz, (double)b->center_z);
    c.set(bhx, (double)b->half_extent_x); c.set(bhz, (double)b->half_extent_z);
    return c.run()[0];
}

/* ── Recursive grammar expansion ────────────────────────────── */

static void srd_expand_recursive(srd_grammar_tree_t *tree,
                                  const srd_symbol_grid_t *grid,
                                  fr_room_box_t *rooms, uint32_t *n_rooms,
                                  uint32_t cap) {
    const srd_rule_t *rules = srd_rule_table();
    int n_rules = srd_rule_count();

    bool expanded_any = true;
    int max_iters = 10;

    while (expanded_any && max_iters-- > 0) {
        expanded_any = false;
        for (int i = 0; i < tree->n_nodes; i++) {
            srd_tree_node_t *n = &tree->nodes[i];
            if (n->type != SRD_NODE_REGION || n->expanded) continue;

            int matches[16];
            int nm = srd_rule_find_matches(grid, n->region_id,
                                            rules, n_rules, matches, 16);
            if (nm > 0) {
                srd_rule_apply(tree, i, &rules[matches[0]], grid);
                expanded_any = true;
            }
        }
    }
}

/* ── Main SRD loop ─────────────────────────────────────────── */

double srd_optimize(fr_room_box_t *rooms, uint32_t *n_rooms, uint32_t cap,
                    fr_corridor_seg_t **corridors, uint32_t *n_corridors,
                    const srd_loss_term_t *terms, uint32_t n_terms,
                    const srd_optimize_config_t *config,
                    const srd_symbol_grid_t *grid,
                    const fr_room_graph_t *graph) {
    if (!rooms || !n_rooms || !config) return 0.0;
    (void)graph;

    srd_optimize_config_t cfg = *config;
    uint32_t n = *n_rooms;
    *n_corridors = 0;
    *corridors = NULL;

    /* ── SRD loop: structural rewrites + gradient descent ─── */
    time_t t0 = time(NULL);
    int step = 0;

    while (step < cfg.max_steps) {
        if (difftime(time(NULL), t0) >= cfg.time_budget_s) break;

        /* ── Evaluate current loss ─────── */
        double total_loss = 0.0;
        for (uint32_t i = 0; i < n; i++) {
            double gx, gz, ghx, ghz;
            room_gradient_symx(&rooms[i], &gx, &gz, &ghx, &ghz);

            /* Gradient descent step */
            rooms[i].center_x      -= (float)(cfg.lr * gx);
            rooms[i].center_z      -= (float)(cfg.lr * gz);
            rooms[i].half_extent_x -= (float)(cfg.lr * ghx);
            rooms[i].half_extent_z -= (float)(cfg.lr * ghz);
            if (rooms[i].half_extent_x < 1.0f) rooms[i].half_extent_x = 1.0f;
            if (rooms[i].half_extent_z < 1.0f) rooms[i].half_extent_z = 1.0f;
        }

        /* NonPenetration loss */
        for (uint32_t i = 0; i < n; i++)
            for (uint32_t j = i + 1; j < n; j++)
                total_loss += overlap_value(&rooms[i], &rooms[j]);

        /* ── Structural rewrites ──────── */
        if (step > 0 && step % cfg.rewrite_interval == 0) {
            fr_rewrite_proposal_t props[64];
            uint32_t pc = 0;
            procgen_srd_propose_rewrites_multiple(rooms, n, 2.0f, 6.0f,
                                                   props, 64, &pc);

            for (uint32_t pi = 0; pi < pc && pi < 8; pi++) {
                fr_room_box_t saved[64];
                memcpy(saved, rooms, n * sizeof(fr_room_box_t));
                uint32_t sn = n;
                void *ptrs[64];
                for (uint32_t j = 0; j < n; j++) ptrs[j] = &rooms[j];

                if (procgen_srd_apply_rewrite(ptrs, &n, cap, &props[pi]) == 0) {
                    /* Evaluate proposal energy */
                    double prop_loss = 0.0;
                    for (uint32_t j = 0; j < n; j++) {
                        fr_room_box_t *r = (fr_room_box_t*)ptrs[j];
                        /* Quick energy check */
                        double hy = (r->ceil_z - r->floor_z) * 0.5;
                        symx::Workspace ws;
                        auto vcx=ws.make_scalar(), vcz=ws.make_scalar();
                        auto vhx=ws.make_scalar(), vhy=ws.make_scalar(), vhz=ws.make_scalar();
                        vhy.set_value(hy);
                        symx::Scalar e = srd_room_sdf_energy(ws, vcx, vcz, vhx, vhy, vhz, 0.5);
                        symx::Compiled<double> cc({ e }, "prop_ev", "../codegen_srd");
                        cc.set(vcx, (double)r->center_x);
                        cc.set(vcz, (double)r->center_z);
                        cc.set(vhx, (double)r->half_extent_x);
                        cc.set(vhz, (double)r->half_extent_z);
                        prop_loss += cc.run()[0];
                    }
                    if (prop_loss < total_loss * cfg.min_improvement
                        || props[pi].type == FR_REWRITE_ADD_CONNECTION) {
                        for (uint32_t j = 0; j < n; j++)
                            memcpy(&rooms[j], ptrs[j], sizeof(fr_room_box_t));
                        *n_rooms = n;
                        total_loss = prop_loss;
                    } else {
                        memcpy(rooms, saved, sn * sizeof(fr_room_box_t));
                        n = sn;
                    }
                } else {
                    memcpy(rooms, saved, sn * sizeof(fr_room_box_t));
                    n = sn;
                }
            }
        }
        step++;
    }

    /* ── Build corridors from adjacency ───── */
    if (grid && grid->n_edges > 0) {
        int nc = 0;
        fr_corridor_seg_t *corrs = (fr_corridor_seg_t*)calloc(
            grid->n_edges, sizeof(fr_corridor_seg_t));

        int rid_to_room[256];
        for (int j = 0; j < 256; j++) rid_to_room[j] = -1;
        for (uint32_t j = 0; j < n; j++) {
            for (int ri = 0; ri < grid->n_regions; ri++) {
                if (rid_to_room[ri] == -1
                    && grid->regions[ri].type_char == rooms[j].type_char) {
                    rid_to_room[ri] = (int)j;
                    break;
                }
            }
        }

        for (int e = 0; e < grid->n_edges && nc < 64; e++) {
            int ria = rid_to_room[grid->edges[e].a];
            int rib = rid_to_room[grid->edges[e].b];
            if (ria < 0 || rib < 0) continue;
            corrs[nc].from_x  = rooms[ria].center_x;
            corrs[nc].from_z  = rooms[ria].center_z;
            corrs[nc].to_x    = rooms[rib].center_x;
            corrs[nc].to_z    = rooms[rib].center_z;
            corrs[nc].width   = 2.0f;
            corrs[nc].floor_z = 0.0f;
            corrs[nc].ceil_z  = 4.0f;
            nc++;
        }
        *corridors = corrs;
        *n_corridors = (uint32_t)nc;
    }

    return 0.0;
}

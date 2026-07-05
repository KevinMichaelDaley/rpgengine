#include "ferrum/procgen/srd/srd_optimizer.h"

#include <symx>
#include "ferrum/procgen/srd/srd_energy.h"
#include "ferrum/procgen/procgen_srd_grammar.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>

using srd::srd_room_sdf_energy;
using srd::srd_overlap_energy;

void srd_optimize_config_default(srd_optimize_config_t *cfg) {
    cfg->time_budget_s    = 5.0;
    cfg->max_steps        = 2000;
    cfg->rewrite_interval = 100;
    cfg->lr               = 0.5;
    cfg->min_improvement  = 0.99;
}

/* ── Build SymX expression for energy + its gradient ──────────── */

static void build_energy_and_gradient(
    symx::Workspace &ws,
    double cx, double cz, double hx, double hy, double hz,
    double *energy_out, double *gx, double *gz, double *ghx, double *ghz) {

    auto v_cx = ws.make_scalar(), v_cz = ws.make_scalar();
    auto v_hx = ws.make_scalar(), v_hy = ws.make_scalar(), v_hz = ws.make_scalar();

    symx::Scalar E = srd_room_sdf_energy(ws, v_cx, v_cz, v_hx, v_hy, v_hz, 0.5);

    std::vector<symx::Scalar> wrt = { v_cx, v_cz, v_hx, v_hz };
    std::vector<symx::Scalar> E_and_grad = symx::value_gradient(E, wrt);
    /* E_and_grad = [E, dE/dcx, dE/dcz, dE/dhx, dE/dhz] */

    symx::Compiled<double> c(E_and_grad, "room_grad", "../codegen_srd");
    c.set(v_cx, cx); c.set(v_cz, cz);
    c.set(v_hx, hx); c.set(v_hy, hy); c.set(v_hz, hz);

    symx::View<double> r = c.run();
    *energy_out = r[0];
    *gx  = r[1];
    *gz  = r[2];
    *ghx = r[3];
    *ghz = r[4];
}

/* ── Overlap energy + gradient via value_gradient ─────────────── */

static double build_overlap_value(symx::Workspace &ws,
    double ax, double az, double ahx, double ahy, double ahz,
    double bx, double bz, double bhx, double bhy, double bhz) {

    auto a_cx = ws.make_scalar(), a_cz = ws.make_scalar();
    auto a_hx = ws.make_scalar(), a_hy = ws.make_scalar(), a_hz = ws.make_scalar();
    auto b_cx = ws.make_scalar(), b_cz = ws.make_scalar();
    auto b_hx = ws.make_scalar(), b_hy = ws.make_scalar(), b_hz = ws.make_scalar();

    symx::Scalar E = srd_overlap_energy(ws,
        a_cx, a_cz, a_hx, a_hy, a_hz,
        b_cx, b_cz, b_hx, b_hy, b_hz, 0.5);

    symx::Compiled<double> c({ E }, "overlap_val", "../codegen_srd");
    c.set(a_cx, ax); c.set(a_cz, az);
    c.set(a_hx, ahx); c.set(a_hy, ahy); c.set(a_hz, ahz);
    c.set(b_cx, bx); c.set(b_cz, bz);
    c.set(b_hx, bhx); c.set(b_hy, bhy); c.set(b_hz, bhz);
    return c.run()[0];
}

/* ── Main SRD loop ──────────────────────────────────────────────── */

double srd_optimize(fr_room_box_t *rooms, uint32_t *n_rooms, uint32_t cap,
                    fr_corridor_seg_t *corridors, uint32_t *n_corridors,
                    const srd_loss_term_t *terms, uint32_t n_terms,
                    const srd_optimize_config_t *config) {
    if (!rooms || !n_rooms || !config) return 0.0;
    (void)corridors; (void)n_corridors; (void)terms; (void)n_terms;

    srd_optimize_config_t cfg = *config;
    time_t t0 = time(NULL);
    uint32_t n = *n_rooms;
    int step = 0;

    while (step < cfg.max_steps) {
        if (difftime(time(NULL), t0) >= cfg.time_budget_s) break;

        double total_e = 0.0;

        for (uint32_t i = 0; i < n; i++) {
            double hy = (rooms[i].ceil_z - rooms[i].floor_z) * 0.5;
            double e, gx, gz, ghx, ghz;
            symx::Workspace ws;
            build_energy_and_gradient(ws,
                rooms[i].center_x, rooms[i].center_z,
                rooms[i].half_extent_x, hy, rooms[i].half_extent_z,
                &e, &gx, &gz, &ghx, &ghz);

            total_e += e;

            /* Gradient descent step */
            rooms[i].center_x      -= (float)(cfg.lr * gx);
            rooms[i].center_z      -= (float)(cfg.lr * gz);
            rooms[i].half_extent_x -= (float)(cfg.lr * ghx);
            rooms[i].half_extent_z -= (float)(cfg.lr * ghz);

            if (rooms[i].half_extent_x < 1.0f) rooms[i].half_extent_x = 1.0f;
            if (rooms[i].half_extent_z < 1.0f) rooms[i].half_extent_z = 1.0f;
        }

        /* NonPenetration via SymX overlap (energy only, no gradient) */
        for (uint32_t i = 0; i < n; i++) {
            for (uint32_t j = i + 1; j < n; j++) {
                double ahy = (rooms[i].ceil_z - rooms[i].floor_z) * 0.5;
                double bhy = (rooms[j].ceil_z - rooms[j].floor_z) * 0.5;
                symx::Workspace ws;
                total_e += build_overlap_value(ws,
                    rooms[i].center_x, rooms[i].center_z,
                    rooms[i].half_extent_x, ahy, rooms[i].half_extent_z,
                    rooms[j].center_x, rooms[j].center_z,
                    rooms[j].half_extent_x, bhy, rooms[j].half_extent_z);
            }
        }

        /* Structural rewrites */
        if (step > 0 && step % cfg.rewrite_interval == 0) {
            fr_rewrite_proposal_t props[64];
            uint32_t pc = 0;
            procgen_srd_propose_rewrites_multiple(rooms, n, 2.0f, 6.0f,
                                                   props, 64, &pc);
            for (uint32_t pi = 0; pi < pc && pi < 8; pi++) {
                fr_room_box_t saved[64]; memcpy(saved, rooms, n * sizeof(fr_room_box_t));
                uint32_t sn = n;
                void *ptrs[64];
                for (uint32_t j = 0; j < n; j++) ptrs[j] = &rooms[j];
                if (procgen_srd_apply_rewrite(ptrs, &n, cap, &props[pi]) == 0) {
                    for (uint32_t j = 0; j < n; j++)
                        memcpy(&rooms[j], ptrs[j], sizeof(fr_room_box_t));
                    *n_rooms = n;
                } else {
                    memcpy(rooms, saved, sn * sizeof(fr_room_box_t));
                    n = sn;
                }
            }
        }
        step++;
    }

    return 0.0;
}

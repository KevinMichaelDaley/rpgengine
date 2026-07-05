#include "ferrum/procgen/srd/srd_bridge.h"
#include "ferrum/procgen/procgen_ascii_parse.h"
#include "ferrum/procgen/srd/srd_loss_compiler.h"
#include "ferrum/procgen/srd/srd_optimizer.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>

int srd_generate(const char *ascii, uint32_t seed, double time_budget,
                 fr_room_box_t **rooms_out, uint32_t *n_rooms_out,
                 fr_corridor_seg_t **corridors_out, uint32_t *n_corridors_out) {
    if (!ascii || !rooms_out || !n_rooms_out
        || !corridors_out || !n_corridors_out) return -1;

    *rooms_out     = NULL;
    *n_rooms_out   = 0;
    *corridors_out = NULL;
    *n_corridors_out = 0;

    if (seed != 0) srand(seed);
    else srand((uint32_t)time(NULL));

    /* ── Parse ASCII grid ── */
    fr_room_graph_t graph;
    if (procgen_ascii_parse(ascii, &graph) != 0) return -1;

    /* ── Extract LOSS: block ── */
    srd_loss_term_t terms[32];
    uint32_t n_terms = 0;
    srd_loss_compile(ascii, &graph, graph.node_count, terms, 32, &n_terms);

    /* ── Create initial rooms from graph ── */
    uint32_t room_cap = graph.node_count * 4; /* allow splits */
    fr_room_box_t *rooms = (fr_room_box_t *)calloc(room_cap, sizeof(fr_room_box_t));
    uint32_t n_rooms = 0;
    if (!rooms) { fr_room_graph_destroy(&graph); return -1; }

    for (uint32_t i = 0; i < graph.node_count; i++) {
        if (graph.nodes[i].type == FR_GRAPH_NODE_ROOM) {
            fr_room_box_init(&rooms[n_rooms]);
            rooms[n_rooms].type_char = graph.nodes[i].type_char;
            /* Initial size from cell count */
            float s = sqrtf((float)graph.nodes[i].cell_count) * 2.0f;
            if (s < 2.0f) s = 2.0f;
            rooms[n_rooms].half_extent_x = s;
            rooms[n_rooms].half_extent_z = s;
            rooms[n_rooms].center_x = (graph.nodes[i].first_cell_x + 0.5f) * s;
            rooms[n_rooms].center_z = (graph.nodes[i].first_cell_z + 0.5f) * s;
            rooms[n_rooms].floor_z = 0.0f;
            rooms[n_rooms].ceil_z  = 4.0f;
            strncpy(rooms[n_rooms].name, graph.nodes[i].label,
                    sizeof(rooms[n_rooms].name) - 1);
            n_rooms++;
        }
    }

    /* ── Run SRD optimizer ── */
    srd_optimize_config_t cfg;
    srd_optimize_config_default(&cfg);
    cfg.time_budget_s = time_budget;
    cfg.max_steps     = (int)(time_budget * 200);

    fr_corridor_seg_t *corridors = NULL;
    uint32_t n_corridors = 0;

    srd_optimize(rooms, &n_rooms, room_cap,
                 corridors, &n_corridors,
                 terms, n_terms, &cfg);

    /* ── Output ── */
    *rooms_out     = rooms;
    *n_rooms_out   = n_rooms;
    *corridors_out = corridors;
    *n_corridors_out = n_corridors;

    fr_room_graph_destroy(&graph);
    return 0;
}

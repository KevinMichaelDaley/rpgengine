/**
 * @file npc_nav_world.c
 * @brief Navigation world lifecycle and query execution.
 *
 * Non-static functions (4 of 4 max):
 *   1. npc_nav_world_init
 *   2. npc_nav_world_destroy
 *   3. npc_nav_world_execute
 */

#include "ferrum/npc/npc_nav_world.h"

#include <stdlib.h>
#include <string.h>

/* ── Lifecycle ──────────────────────────────────────────────────── */

bool npc_nav_world_init(npc_nav_world_t *nw) {
    if (!nw) return false;
    memset(nw, 0, sizeof(*nw));
    return true;
}

void npc_nav_world_destroy(npc_nav_world_t *nw) {
    if (!nw) return;
    npc_nav_hgraph_destroy(&nw->hgraph);
    npc_nav_graph_destroy(&nw->graph);
    npc_svo_grid_destroy(&nw->svo);
    free(nw->dynamic_blockers);
    memset(nw, 0, sizeof(*nw));
}

/* ── Query execution ────────────────────────────────────────────── */

void npc_nav_world_execute(npc_nav_world_t *nw,
                           const uint8_t params[64],
                           uint8_t *result_ptr,
                           uint32_t result_cap) {
    if (!nw || !params || !result_ptr || result_cap == 0) return;

    /* Write error header as default. */
    int32_t *status = (int32_t *)result_ptr;
    uint32_t *wp_count = (uint32_t *)(result_ptr + sizeof(int32_t));
    *status = -1;
    *wp_count = 0;

    /* Extract params. */
    float start[3], goal[3];
    memcpy(start, params, 12);
    memcpy(goal, params + 12, 12);
    uint32_t strategy;
    memcpy(&strategy, params + 24, 4);
    float agent_radius, agent_height;
    memcpy(&agent_radius, params + 28, 4);
    memcpy(&agent_height, params + 32, 4);
    uint32_t max_wp;
    memcpy(&max_wp, params + 36, 4);
    if (max_wp == 0) max_wp = 64;
    if (max_wp > 256) max_wp = 256;

    /* Available space for waypoints after header. */
    uint32_t header_bytes = sizeof(int32_t) + sizeof(uint32_t);
    uint32_t max_count = (result_cap - header_bytes) / sizeof(float) / 3;
    if (max_count > max_wp) max_count = max_wp;

    phys_vec3_t sv = {start[0], start[1], start[2]};
    phys_vec3_t gv = {goal[0], goal[1], goal[2]};

    /* Run SVO A*. */
    phys_vec3_t *wp_buf = (phys_vec3_t *)(result_ptr + header_bytes);
    uint32_t wp_out = 0;

    bool found = npc_svo_astar(&nw->svo,
                               nw->dynamic_blockers,
                               nw->blocker_count,
                               sv, gv,
                               wp_buf, &wp_out,
                               max_count,
                               agent_radius, agent_height);

    *status = found ? 0 : -1;
    *wp_count = wp_out;

    /* Apply shortcut reduction if path found. */
    if (found && wp_out > 2) {
        phys_vec3_t tmp_buf[256];
        uint32_t copy_count = wp_out < 256 ? wp_out : 256;
        memcpy(tmp_buf, wp_buf, copy_count * sizeof(phys_vec3_t));
        uint32_t sc = 0;
        npc_pathfind_shortcut(tmp_buf, copy_count, wp_buf, &sc,
                              max_count, &nw->svo,
                              nw->dynamic_blockers, nw->blocker_count);
        *wp_count = sc;
    }
}

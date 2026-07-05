#include "ferrum/procgen/procgen_srd_types.h"
#include <stdlib.h>
#include <string.h>

void fr_room_box_init(fr_room_box_t *box) {
    if (!box) return;
    memset(box, 0, sizeof(*box));
    box->floor_z  = 0.0f;
    box->ceil_z   = 3.0f;
    box->material = 1;
}

void fr_floor_def_init(fr_floor_def_t *floor) {
    if (!floor) return;
    memset(floor, 0, sizeof(*floor));
}

void fr_floor_def_destroy(fr_floor_def_t *floor) {
    if (!floor) return;
    free(floor->rooms);
    free(floor->corridors);
    free(floor->stairs);
    memset(floor, 0, sizeof(*floor));
}

void fr_room_graph_init(fr_room_graph_t *graph) {
    if (!graph) return;
    memset(graph, 0, sizeof(*graph));
}

void fr_room_graph_destroy(fr_room_graph_t *graph) {
    if (!graph) return;
    free(graph->nodes);
    free(graph->edges);
    free(graph->stair_pairs);
    memset(graph, 0, sizeof(*graph));
}

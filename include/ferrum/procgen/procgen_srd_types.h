#ifndef FERRUM_PROCGEN_SRD_TYPES_H
#define FERRUM_PROCGEN_SRD_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Room Box ──────────────────────────────────────────────────── */

typedef struct {
    float    center_x;
    float    center_z;
    float    half_extent_x;
    float    half_extent_z;
    float    floor_z;
    float    ceil_z;
    uint16_t material;
    char     type_char;
    char     name[64];
} fr_room_box_t;

void fr_room_box_init(fr_room_box_t *box);

/* ── Corridor Segment ──────────────────────────────────────────── */

typedef struct {
    float from_x;
    float from_z;
    float to_x;
    float to_z;
    float width;
    float floor_z;
    float ceil_z;
} fr_corridor_seg_t;

/* ── Stair Definition ──────────────────────────────────────────── */

typedef enum {
    FR_STAIR_UP   = 0,
    FR_STAIR_DOWN = 1,
} fr_stair_direction_t;

typedef struct {
    float                anchor_x;
    float                anchor_z;
    fr_stair_direction_t direction;
    uint32_t             n_steps;
    float                step_h;
    float                step_d;
    float                floor_from;
    float                floor_to;
} fr_stair_def_t;

/* ── Floor Definition ──────────────────────────────────────────── */

typedef struct {
    uint32_t           room_count;
    fr_room_box_t     *rooms;
    uint32_t           corridor_count;
    fr_corridor_seg_t *corridors;
    uint32_t           stair_count;
    fr_stair_def_t    *stairs;
    float              world_offset_x;
    float              world_offset_z;
} fr_floor_def_t;

void fr_floor_def_init(fr_floor_def_t *floor);
void fr_floor_def_destroy(fr_floor_def_t *floor);

/* ── Room Graph (intermediate representation from ASCII parse) ─── */

typedef enum {
    FR_GRAPH_NODE_ROOM = 0,
    FR_GRAPH_NODE_STAIR_ANCHOR = 1,
} fr_graph_node_type_t;

typedef struct {
    fr_graph_node_type_t type;
    uint32_t             first_cell_x;
    uint32_t             first_cell_z;
    uint32_t             cell_count;
    char                 type_char;
    char                 label[64];
} fr_graph_node_t;

typedef struct {
    uint32_t node_a;
    uint32_t node_b;
} fr_graph_edge_t;

typedef struct {
    uint32_t room_node;
    uint32_t stair_anchor_node;
} fr_graph_stair_pair_t;

typedef struct {
    uint32_t               node_count;
    fr_graph_node_t       *nodes;
    uint32_t               edge_count;
    fr_graph_edge_t       *edges;
    uint32_t               stair_pair_count;
    fr_graph_stair_pair_t *stair_pairs;
} fr_room_graph_t;

void fr_room_graph_init(fr_room_graph_t *graph);
void fr_room_graph_destroy(fr_room_graph_t *graph);

/* ── Loss Primitives ───────────────────────────────────────────── */

typedef enum {
    FR_LOSS_PATH_DISTANCE       = 0,
    FR_LOSS_LINE_OF_SIGHT       = 1,
    FR_LOSS_NON_PENETRATION     = 2,
    FR_LOSS_MINIMUM_SIZE        = 3,
    FR_LOSS_SEPARATION          = 4,
    FR_LOSS_CONTAINMENT         = 5,
    FR_LOSS_ADJACENCY_COUNT     = 6,
    FR_LOSS_HEIGHT_SPAN         = 7,
    FR_LOSS_STAIR_ALIGNMENT     = 8,
    FR_LOSS_FLOOR_ACCESSIBILITY = 9,
} fr_loss_primitive_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROCGEN_SRD_TYPES_H */

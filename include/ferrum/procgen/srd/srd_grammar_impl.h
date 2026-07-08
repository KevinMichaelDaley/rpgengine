#ifndef FERRUM_PROCGEN_SRD_GRAMMAR_IMPL_H
#define FERRUM_PROCGEN_SRD_GRAMMAR_IMPL_H

#include <stdint.h>
#include "ferrum/procgen/procgen_srd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SRD_MAX_TREE_NODES 1024

typedef enum {
    SRD_NODE_REGION = 0,       /* non-terminal: expand to room(s) */
    SRD_NODE_CONNECTION = 1,   /* non-terminal: expand to corridor */
    SRD_NODE_STAIR = 2,        /* non-terminal: expand to steps */
    SRD_NODE_ROOM = 3,         /* terminal: fr_room_box_t */
    SRD_NODE_CORRIDOR = 4,     /* terminal: fr_corridor_seg_t */
    SRD_NODE_WALL = 5,         /* terminal: wall segment */
    SRD_NODE_OPENING = 6,      /* terminal: door/opening */
} srd_node_type_t;

typedef struct srd_tree_node {
    srd_node_type_t type;
    int             parent;
    int             children[8];
    int             n_children;

    /* Region/terminal parameters (context-sensitive) */
    float           cx, cz;              /* center */
    float           hx, hz;              /* half-extent */
    float           floor_z, ceil_z;     /* height */
    char            type_char;           /* R, B, G, P, etc. */
    int             region_id;           /* source region in symbol grid */
    int             adjacent_regions[4]; /* neighbor region IDs */
    int             n_adjacent;

    /* For corridors */
    float           from_x, from_z;
    float           to_x, to_z;

    /* For stairs */
    int             stair_dir;           /* 0=up, 1=down */

    /* Expansion state */
    int             expanded;            /* 0=non-terminal, 1=expanded to children */
    double          energy;              /* current energy for this subtree */
} srd_tree_node_t;

typedef struct {
    srd_tree_node_t nodes[SRD_MAX_TREE_NODES];
    int              n_nodes;
    double           total_energy;
} srd_grammar_tree_t;

void srd_grammar_tree_init(srd_grammar_tree_t *tree);
int  srd_grammar_tree_add_node(srd_grammar_tree_t *tree,
                                srd_node_type_t type, int parent);
void srd_grammar_tree_add_child(srd_grammar_tree_t *tree,
                                 int parent, int child);

/**
 * @brief Build initial grammar tree from a symbol grid.
 * Each region becomes a SRD_NODE_REGION with context info.
 */
int srd_grammar_tree_init_from_grid(srd_grammar_tree_t *tree,
                                     const struct srd_symbol_grid_t *grid);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROCGEN_SRD_GRAMMAR_IMPL_H */

#include "ferrum/procgen/srd/srd_grammar_impl.h"
#include "ferrum/procgen/srd/srd_symbol_grid.h"
#include <string.h>

void srd_grammar_tree_init(srd_grammar_tree_t *tree) {
    memset(tree, 0, sizeof(*tree));
}

int srd_grammar_tree_add_node(srd_grammar_tree_t *tree,
                               srd_node_type_t type, int parent) {
    if (!tree || tree->n_nodes >= SRD_MAX_TREE_NODES) return -1;
    int idx = tree->n_nodes++;
    memset(&tree->nodes[idx], 0, sizeof(tree->nodes[idx]));
    tree->nodes[idx].type   = type;
    tree->nodes[idx].parent = parent;
    return idx;
}

void srd_grammar_tree_add_child(srd_grammar_tree_t *tree,
                                 int parent, int child) {
    if (!tree || parent < 0 || child < 0) return;
    srd_tree_node_t *p = &tree->nodes[parent];
    if (p->n_children < 8) {
        p->children[p->n_children++] = child;
        tree->nodes[child].parent = parent;
    }
}

int srd_grammar_tree_init_from_grid(srd_grammar_tree_t *tree,
                                     const srd_symbol_grid_t *grid) {
    if (!tree || !grid) return -1;
    srd_grammar_tree_init(tree);

    for (int ri = 0; ri < grid->n_regions; ri++) {
        const srd_region_t *r = &grid->regions[ri];
        if (r->type_char == 'W' || r->type_char == '.') continue;

        int node_id = srd_grammar_tree_add_node(tree, SRD_NODE_REGION, -1);
        if (node_id < 0) continue;

        srd_tree_node_t *n = &tree->nodes[node_id];
        n->type_char = r->type_char;
        n->region_id = ri;
        float s = 2.0f;
        n->cx = (r->first_cell.x + 0.5f) * s;
        n->cz = (r->first_cell.z + 0.5f) * s;
        n->hx = s * 0.6f;
        n->hz = s * 0.6f;
        n->floor_z = 0.0f;
        n->ceil_z  = 4.0f;
        n->n_adjacent = srd_symbol_grid_adjacent_regions(grid, ri,
                            n->adjacent_regions, 4);
        if (r->label[0])
            strncpy((char*)&(r->label), r->label, 63);
    }
    return 0;
}

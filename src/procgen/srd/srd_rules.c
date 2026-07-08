#include "ferrum/procgen/srd/srd_rules.h"
#include <string.h>

#define C(sym)        { SRD_COND_ADJACENT_SYMBOL, (sym), 0 }
#define C_CELL_GE(n)  { SRD_COND_CELL_COUNT_GE, 0, (int)(n) }
#define C_ANY         { SRD_COND_ADJACENT_ANY, 0, 0 }
#define C_BOUNDARY    { SRD_COND_ON_BOUNDARY, 0, 0 }
#define C_STAIR       { SRD_COND_HAS_STAIR, 0, 0 }

/* Params: [0]=width, [1]=position_frac, [2]=height, [3]=extent_x, [4]=extent_z */
static const srd_rule_t RULES[] = {

    /* ── R: Common Room ─────────────────────────────── */
    /* Rule 0: Basic room */
    { 'R', 0, {}, { SRD_EXPAND_ROOM, {3.0f,0,4.0f,6.0f,6.0f}, -1, 0 }, 4, {0,2,3,4} },
    /* Rule 1: Room with door toward adjacent region */
    { 'R', 1, { C_ANY }, { SRD_EXPAND_ROOM_DOOR, {1.0f,0,4.0f,6.0f,6.0f}, -1, 0 }, 4, {0,2,3,4} },
    /* Rule 2: Large room → split */
    { 'R', 1, { C_CELL_GE(8) }, { SRD_EXPAND_SPLIT, {0, 0.5f}, -1, 0 }, 1, {1} },
    /* Rule 3: Room with wall on one side (if adjacent to W) */
    { 'R', 1, { C('W') }, { SRD_EXPAND_ROOM_WALL, {0, 0, 4.0f, 6.0f, 6.0f}, -1, 0 }, 4, {2,3,4} },
    /* Rule 4: Room with stair anchor */
    { 'R', 1, { C('^') }, { SRD_EXPAND_STAIR, {0.25f, 0.5f, 20}, -1, 0 }, 3, {0,1,2} },

    /* ── B: Bar ─────────────────────────────────────── */
    { 'B', 0, {}, { SRD_EXPAND_ROOM, {3.0f,0,4.0f,5.0f,5.0f}, -1, 0 }, 4, {0,2,3,4} },
    { 'B', 1, { C_ANY }, { SRD_EXPAND_ROOM_DOOR, {2.0f,0,4.0f,5.0f,5.0f}, -1, 0 }, 4, {0,2,3,4} },
    { 'B', 1, { C('W') }, { SRD_EXPAND_ROOM_WALL, {0,0,4.0f,5.0f,5.0f}, -1, 0 }, 4, {2,3,4} },

    /* ── G: Entrance ─────────────────────────────────── */
    { 'G', 0, {}, { SRD_EXPAND_ROOM, {2.0f,0,4.0f,5.0f,5.0f}, -1, 0 }, 4, {0,2,3,4} },
    { 'G', 1, { C('W') }, { SRD_EXPAND_ROOM_DOOR, {2.0f,0,4.0f,5.0f,5.0f}, -1, 0 }, 4, {0,2,3,4} },
    { 'G', 1, { C_ANY }, { SRD_EXPAND_ROOM_DOOR, {1.0f,0,4.0f,5.0f,5.0f}, -1, 0 }, 4, {0,2,3,4} },

    /* ── P: Private Room ─────────────────────────────── */
    { 'P', 0, {}, { SRD_EXPAND_ROOM, {1.0f,0,3.0f,4.0f,4.0f}, -1, 0 }, 4, {0,2,3,4} },
    { 'P', 1, { C_ANY }, { SRD_EXPAND_ROOM_DOOR, {1.0f,0,3.0f,4.0f,4.0f}, -1, 0 }, 4, {0,2,3,4} },
    { 'P', 1, { C('W') }, { SRD_EXPAND_ROOM_WALL, {0,0,3.0f,4.0f,4.0f}, -1, 0 }, 4, {2,3,4} },

    /* ── ^: Stairs Up ────────────────────────────────── */
    { '^', 0, {}, { SRD_EXPAND_STAIR, {0.25f, 0.5f, 20}, -1, 0 }, 3, {0,1,2} },

    /* ── v: Stairs Down ──────────────────────────────── */
    { 'v', 0, {}, { SRD_EXPAND_STAIR, {0.25f, 0.5f, 20}, -1, 1 }, 3, {0,1,2} },

    /* ── W: Wall ─────────────────────────────────────── */
    /* Wall between two adjacent regions */
    { 'W', 0, {}, { SRD_EXPAND_ROOM_WALL, {0,0,4.0f,0,0}, -1, 0 }, 0, {} },

    /* ── .: Floor ────────────────────────────────────── */
    /* Open floor merges into adjacent room */
    { '.', 0, {}, { SRD_EXPAND_ROOM, {0,0,4.0f,0,0}, -1, 0 }, 0, {} },
};
#undef C
#undef C_CELL_GE
#undef C_ANY
#undef C_BOUNDARY
#undef C_STAIR

static const int N_RULES = sizeof(RULES) / sizeof(RULES[0]);

/* ── Condition checking ───────────────────────────────────────── */

static bool check_adjacent_symbol(const srd_symbol_grid_t *grid,
                                   int region_id, char symbol) {
    int adj[8];
    int n = srd_symbol_grid_adjacent_regions(grid, region_id, adj, 8);
    for (int i = 0; i < n; i++)
        if (adj[i] >= 0 && adj[i] < grid->n_regions
            && grid->regions[adj[i]].type_char == symbol)
            return true;
    return false;
}

static bool check_cell_count_ge(const srd_symbol_grid_t *grid,
                                  int region_id, int min_cells) {
    if (region_id < 0 || region_id >= grid->n_regions) return false;
    return grid->regions[region_id].cell_count >= min_cells;
}

static bool check_on_boundary(const srd_symbol_grid_t *grid,
                               int region_id) {
    /* Check if region touches the grid boundary */
    int x0 = grid->regions[region_id].first_cell.x;
    int z0 = grid->regions[region_id].first_cell.z;
    if (x0 == 0 || x0 == grid->width - 1
        || z0 == 0 || z0 == grid->height - 1)
        return true;
    /* Check all cells for boundary touch */
    for (int z = 0; z < grid->height; z++)
        for (int x = 0; x < grid->width; x++)
            if (grid->region_id[z][x] == region_id)
                if (x == 0 || x == grid->width - 1
                    || z == 0 || z == grid->height - 1)
                    return true;
    return false;
}

static bool check_adjacent_stair(const srd_symbol_grid_t *grid,
                                  int region_id) {
    return check_adjacent_symbol(grid, region_id, '^')
        || check_adjacent_symbol(grid, region_id, 'v');
}

static bool check_condition(const srd_cond_t *c,
                             const srd_symbol_grid_t *grid,
                             int region_id) {
    switch (c->type) {
    case SRD_COND_NONE:           return true;
    case SRD_COND_ADJACENT_SYMBOL: return check_adjacent_symbol(grid, region_id, c->symbol);
    case SRD_COND_ADJACENT_ANY:    return srd_symbol_grid_adjacent_count(grid, region_id) > 0;
    case SRD_COND_CELL_COUNT_GE:   return check_cell_count_ge(grid, region_id, c->int_value);
    case SRD_COND_ON_BOUNDARY:     return check_on_boundary(grid, region_id);
    case SRD_COND_HAS_STAIR:       return check_adjacent_stair(grid, region_id);
    }
    return false;
}

/* ── Public API ────────────────────────────────────────────────── */

int srd_rule_count(void) { return N_RULES; }
const srd_rule_t *srd_rule_table(void) { return RULES; }

bool srd_rule_matches(const srd_rule_t *rule,
                       const srd_symbol_grid_t *grid,
                       int region_id) {
    if (!rule || !grid) return false;

    /* Symbol must match region type */
    srd_region_t *r = &grid->regions[region_id];
    if (rule->symbol != r->type_char) return false;

    /* All conditions must pass */
    for (int i = 0; i < rule->n_conditions; i++)
        if (!check_condition(&rule->conditions[i], grid, region_id))
            return false;

    return true;
}

int srd_rule_apply(srd_grammar_tree_t *tree, int node_id,
                     const srd_rule_t *rule,
                     const srd_symbol_grid_t *grid) {
    if (!tree || node_id < 0) return -1;
    srd_tree_node_t *node = &tree->nodes[node_id];
    node->expanded = 1;

    switch (rule->expansion.type) {
    case SRD_EXPAND_ROOM:
    case SRD_EXPAND_ROOM_DOOR:
    case SRD_EXPAND_ROOM_WALL: {
        /* Create room terminal */
        int room = srd_grammar_tree_add_node(tree, SRD_NODE_ROOM, node_id);
        if (room < 0) return -1;
        srd_grammar_tree_add_child(tree, node_id, room);

        srd_tree_node_t *rn = &tree->nodes[room];
        rn->cx = (float)(grid->regions[node->region_id].first_cell.x + 0.5f)
                 * rule->expansion.param[3];
        rn->cz = (float)(grid->regions[node->region_id].first_cell.z + 0.5f)
                 * rule->expansion.param[4];
        rn->hx = rule->expansion.param[3];
        rn->hz = rule->expansion.param[4];
        rn->floor_z = 0.0f;
        rn->ceil_z  = rule->expansion.param[2];
        rn->type_char = node->type_char;
        break;
    }
    case SRD_EXPAND_STAIR: {
        int stair = srd_grammar_tree_add_node(tree, SRD_NODE_STAIR, node_id);
        if (stair < 0) return -1;
        srd_grammar_tree_add_child(tree, node_id, stair);
        srd_tree_node_t *sn = &tree->nodes[stair];
        sn->cx = node->cx; sn->cz = node->cz;
        sn->stair_dir = rule->expansion.direction;
        break;
    }
    case SRD_EXPAND_CORRIDOR:
    case SRD_EXPAND_SPLIT:
    case SRD_EXPAND_MERGE:
        /* Handled by the optimizer */
        break;
    }
    return 0;
}

int srd_rule_find_matches(const srd_symbol_grid_t *grid,
                            int region_id,
                            const srd_rule_t *rules, int n_rules,
                            int *out_indices, int max_out) {
    if (!grid || !rules || !out_indices) return 0;
    int count = 0;
    for (int i = 0; i < n_rules && count < max_out; i++)
        if (srd_rule_matches(&rules[i], grid, region_id))
            out_indices[count++] = i;
    return count;
}

#include "ferrum/procgen/srd/srd_grammar.h"
#include <string.h>
#include <stdlib.h>

/* ── Symbol helpers ──────────────────────────────────────────── */

static srd_symbol_t char_to_sym(char c) {
    switch (c) {
    case 'R': return SRD_SYM_R;
    case 'B': return SRD_SYM_B;
    case 'G': return SRD_SYM_G;
    case 'P': return SRD_SYM_P;
    case 'W': return SRD_SYM_W;
    case '.': return SRD_SYM_DOT;
    case '^': return SRD_SYM_UP;
    case 'v': return SRD_SYM_DN;
    default:  return SRD_SYM_W;
    }
}

/* ── Grammar tree ────────────────────────────────────────────── */

void srd_grammar_init(srd_grammar_t *g) { memset(g, 0, sizeof(*g)); }

int srd_grammar_add_node(srd_grammar_t *g, srd_symbol_t sym,
                          int parent, int rule_id) {
    if (!g || g->n_nodes >= SRD_MAX_TREE_NODES) return -1;
    int id = g->n_nodes++;
    memset(&g->nodes[id], 0, sizeof(g->nodes[id]));
    g->nodes[id].id        = id;
    g->nodes[id].symbol    = sym;
    g->nodes[id].parent_id = parent;
    g->nodes[id].rule_id   = rule_id;
    g->nodes[id].expanded  = false;
    if (parent >= 0 && parent < g->n_nodes) {
        srd_tree_node_t *p = &g->nodes[parent];
        if (p->n_children < 8) p->child_ids[p->n_children++] = id;
    }
    return id;
}

/* ── 2D Grid ─────────────────────────────────────────────────── */

int srd_grid_parse(const char **lines, int n, srd_grid_t *grid) {
    if (!lines || !grid || n < 1) return -1;
    memset(grid, 0, sizeof(*grid));
    grid->height = n;
    for (int z = 0; z < n; z++) {
        int w = 0;
        for (const char *p = lines[z]; *p; p++)
            if (*p != ' ') w++;
        if (w > grid->width) grid->width = w;
    }
    int total = grid->width * grid->height;
    grid->cells     = (char *)malloc(total);
    grid->region_ids = (int *)malloc(total * sizeof(int));
    grid->labels    = (int *)malloc(total * sizeof(int));

    for (int z = 0; z < grid->height; z++) {
        int x = 0;
        for (const char *p = lines[z]; *p; p++)
            if (*p != ' ') { grid->cells[z * grid->width + x] = *p; x++; }
    }
    for (int i = 0; i < total; i++) {
        grid->region_ids[i] = -1;
        grid->labels[i]     = -1;
    }

    /* Flood-fill regions */
    int nreg = 0;
    for (int z = 0; z < grid->height; z++) {
        for (int x = 0; x < grid->width; x++) {
            if (grid->region_ids[z * grid->width + x] != -1) continue;
            char ch = grid->cells[z * grid->width + x];
            if (ch == 'W' || ch == '.') continue;

            int rid = nreg++;
            static int sx[1024], sz[1024]; int sp = 0;
            sx[sp] = x; sz[sp] = z; sp++;
            grid->region_ids[z * grid->width + x] = rid;

            int count = 0;
            while (sp > 0) {
                sp--; int cx = sx[sp], cz = sz[sp]; count++;
                for (int d = 0; d < 4; d++) {
                    static const int dx[] = {-1,1,0,0}, dz[] = {0,0,-1,1};
                    int nx = cx + dx[d], nz = cz + dz[d];
                    if (nx < 0 || nx >= grid->width || nz < 0 || nz >= grid->height) continue;
                    if (grid->region_ids[nz * grid->width + nx] != -1) continue;
                    char nc = grid->cells[nz * grid->width + nx];
                    if (nc != ch && nc != '.') continue;
                    grid->region_ids[nz * grid->width + nx] = rid;
                    sx[sp] = nx; sz[sp] = nz; sp++;
                }
            }
            grid->regions[rid].id         = rid;
            grid->regions[rid].type_char  = ch;
            grid->regions[rid].first_x    = x;
            grid->regions[rid].first_z    = z;
            grid->regions[rid].cell_count = count;
            grid->regions[rid].label[0]   = 0;
            grid->n_regions = nreg;
        }
    }

    /* Edges */
    int ne = 0;
    for (int z = 0; z < grid->height; z++) {
        for (int x = 0; x < grid->width; x++) {
            int a = grid->region_ids[z * grid->width + x];
            if (a < 0) continue;
            if (x+1 < grid->width) {
                int b = grid->region_ids[z * grid->width + x + 1];
                if (b >= 0 && a != b) {
                    bool dup = false;
                    for (int e = 0; e < ne; e++)
                        if ((grid->edges[e].a==a && grid->edges[e].b==b) ||
                            (grid->edges[e].a==b && grid->edges[e].b==a))
                            { dup = true; break; }
                    if (!dup) { grid->edges[ne].a=a; grid->edges[ne].b=b; ne++; }
                }
            }
            if (z+1 < grid->height) {
                int b = grid->region_ids[(z+1) * grid->width + x];
                if (b >= 0 && a != b) {
                    bool dup = false;
                    for (int e = 0; e < ne; e++)
                        if ((grid->edges[e].a==a && grid->edges[e].b==b) ||
                            (grid->edges[e].a==b && grid->edges[e].b==a))
                            { dup = true; break; }
                    if (!dup) { grid->edges[ne].a=a; grid->edges[ne].b=b; ne++; }
                }
            }
        }
    }
    grid->n_edges = ne;
    return 0;
}

int srd_grid_region_at(const srd_grid_t *g, int x, int z) {
    if (!g || x < 0 || x >= g->width || z < 0 || z >= g->height) return -1;
    return g->region_ids[z * g->width + x];
}

int srd_grid_adjacent_regions(const srd_grid_t *g, int rid,
                               int *out, int max) {
    if (!g || rid < 0 || !out) return 0;
    int n = 0;
    for (int e = 0; e < g->n_edges && n < max; e++) {
        if (g->edges[e].a == rid) out[n++] = g->edges[e].b;
        else if (g->edges[e].b == rid) out[n++] = g->edges[e].a;
    }
    return n;
}

srd_symbol_t srd_grid_region_symbol(const srd_grid_t *g, int rid) {
    if (!g || rid < 0 || rid >= (int)g->n_regions) return SRD_SYM_W;
    return char_to_sym(g->regions[rid].type_char);
}

/* ── Rule matching ───────────────────────────────────────────── */

static int count_adjacent_of_type(const srd_grid_t *g, int rid, srd_symbol_t sym) {
    int adj[8], count = 0;
    int n = srd_grid_adjacent_regions(g, rid, adj, 8);
    for (int i = 0; i < n; i++)
        if (srd_grid_region_symbol(g, adj[i]) == sym) count++;
    return count;
}

bool srd_rule_matches(const srd_rule_t *r, const srd_grid_t *g, int rid) {
    if (!r || !g || rid < 0) return false;
    if (r->symbol != srd_grid_region_symbol(g, rid)) return false;
    for (int i = 0; i < r->n_conditions; i++) {
        const srd_condition_t *c = &r->conditions[i];
        switch (c->type) {
        case SRD_COND_NONE: break;
        case SRD_COND_ADJACENT_HAS:
            if (count_adjacent_of_type(g, rid, c->sym) == 0) return false;
            break;
        case SRD_COND_ADJACENT_COUNT_GE: {
            int adj[8];
            if (srd_grid_adjacent_regions(g, rid, adj, 8) < c->ival) return false;
            break;
        }
        case SRD_COND_CELL_COUNT_GE:
            if (g->regions[rid].cell_count < c->ival) return false;
            break;
        case SRD_COND_ADJACENT_COUNT_LE: {
            int adj[8];
            if (srd_grid_adjacent_regions(g, rid, adj, 8) > c->ival) return false;
            break;
        }
        }
    }
    return true;
}

int srd_rule_find_all(const srd_grid_t *g, int rid,
                       const srd_rule_t *rules, int n,
                       int *out, int max) {
    if (!g || !out) return 0;
    int count = 0;
    for (int i = 0; i < n && count < max; i++)
        if (srd_rule_matches(&rules[i], g, rid))
            out[count++] = i;
    return count;
}

/* ── Rule application ────────────────────────────────────────── */

int srd_rule_apply_to_node(srd_grammar_t *gram, int node_id,
                            const srd_rule_t *rule,
                            const srd_grid_t *grid) {
    if (!gram || node_id < 0 || !rule) return -1;
    srd_tree_node_t *n = &gram->nodes[node_id];

    /* Copy default params */
    for (int i = 0; i < rule->n_params; i++) {
        int p = rule->params[i];
        n->params[p] = rule->defaults[p];
    }

    /* Context: room center from grid position */
    if (grid && n->region_id >= 0 && n->region_id < grid->n_regions) {
        n->params[2] = grid->regions[n->region_id].first_x * 2.0f + 2.0f;
        n->params[3] = grid->regions[n->region_id].first_z * 2.0f + 2.0f;
    }

    /* Create child terminals based on action */
    switch (rule->action) {
    case SRD_ACT_ROOM: {
        /* Terminal: room box */
        int child = srd_grammar_add_node(gram, rule->symbol, node_id, rule->id);
        if (child >= 0) {
            srd_tree_node_t *c = &gram->nodes[child];
            memcpy(c->params, n->params, sizeof(n->params));
            c->expanded = true;
        }
        break;
    }
    case SRD_ACT_CONNECT: {
        /* Non-terminal: connection between two regions */
        int adj[8];
        int na = srd_grid_adjacent_regions(grid, n->region_id, adj, 8);
        if (na > 0) {
            int child = srd_grammar_add_node(gram, rule->symbol, node_id, rule->id);
            if (child >= 0) {
                srd_tree_node_t *c = &gram->nodes[child];
                c->connect_from = node_id;
                c->connect_to   = -1; /* filled by optimizer */
                c->params[0]    = 2.0f; /* width */
                /* Then create corridor terminal */
                int corr = srd_grammar_add_node(gram, rule->symbol, child, rule->id);
                if (corr >= 0) {
                    gram->nodes[corr].expanded = true;
                    memcpy(gram->nodes[corr].params, c->params, sizeof(c->params));
                }
            }
        }
        break;
    }
    case SRD_ACT_SUB_REGION:
    case SRD_ACT_CORRIDOR:
    case SRD_ACT_WALL_SEG:
    case SRD_ACT_STAIR_STEP:
    case SRD_ACT_OPENING:
        break;
    }

    n->expanded = true;
    return 0;
}

/* ── Geometry extraction ─────────────────────────────────────── */

int srd_grammar_extract(const srd_grammar_t *gram,
                         fr_room_box_t **rooms, uint32_t *n_rooms,
                         fr_corridor_seg_t **corrs, uint32_t *n_corrs) {
    if (!gram || !rooms || !n_rooms || !corrs || !n_corrs) return -1;
    *rooms = NULL; *n_rooms = 0;
    *corrs = NULL; *n_corrs = 0;

    fr_room_box_t *r_out = (fr_room_box_t*)calloc(gram->n_nodes, sizeof(fr_room_box_t));
    fr_corridor_seg_t *c_out = (fr_corridor_seg_t*)calloc(gram->n_nodes, sizeof(fr_corridor_seg_t));
    uint32_t nr = 0, nc = 0;

    for (int i = 0; i < gram->n_nodes; i++) {
        const srd_tree_node_t *n = &gram->nodes[i];
        if (!n->expanded) continue;
        if (n->symbol == SRD_SYM_W || n->symbol == SRD_SYM_DOT) continue;

        /* All expanded terminals with extent > 0 are rooms */
        if (n->params[2] > 0.5f && n->params[3] > 0.5f) {
            fr_room_box_init(&r_out[nr]);
            r_out[nr].center_x      = n->params[2];
            r_out[nr].center_z      = n->params[3];
            r_out[nr].half_extent_x = n->params[2] > 3 ? n->params[2] / 2 : 4.0f;
            r_out[nr].half_extent_z = n->params[3] > 3 ? n->params[3] / 2 : 4.0f;
            r_out[nr].floor_z       = 0.0f;
            r_out[nr].ceil_z        = n->params[1] > 0 ? n->params[1] : 4.0f;
            switch (n->symbol) {
            case SRD_SYM_R: r_out[nr].type_char = 'R'; break;
            case SRD_SYM_B: r_out[nr].type_char = 'B'; break;
            case SRD_SYM_G: r_out[nr].type_char = 'G'; break;
            case SRD_SYM_P: r_out[nr].type_char = 'P'; break;
            default: break;
            }
            nr++;
        }

        /* Connection/corridor nodes → corridor segments */
        if (n->connect_from >= 0 && n->connect_to >= 0
            && n->connect_to < gram->n_nodes) {
            const srd_tree_node_t *from = &gram->nodes[n->connect_from];
            const srd_tree_node_t *to   = &gram->nodes[n->connect_to];
            memset(&c_out[nc], 0, sizeof(c_out[0]));
            c_out[nc].from_x  = from->params[2];
            c_out[nc].from_z  = from->params[3];
            c_out[nc].to_x    = to->params[2];
            c_out[nc].to_z    = to->params[3];
            c_out[nc].width   = n->params[0] > 0 ? n->params[0] : 2.0f;
            c_out[nc].floor_z = 0.0f;
            c_out[nc].ceil_z  = 4.0f;
            nc++;
        }
    }

    *rooms = r_out; *n_rooms = nr;
    *corrs = c_out; *n_corrs = nc;
    return 0;
}

/* ── Rule table (data-driven, context-sensitive) ─────────────── */

#define R(id,sym,conds,act,np,...) \
    [id] = { id, sym, sizeof((srd_condition_t[]){conds})/sizeof(srd_condition_t), \
             conds, act, np, __VA_ARGS__ }

static const srd_rule_t RULE_TABLE[] = {
    /* R: Common Room — multiple expansion paths */
    { 0,  SRD_SYM_R, 0, {{0}}, SRD_ACT_ROOM, 4,
      {SRD_PARAM_WIDTH,SRD_PARAM_HEIGHT,SRD_PARAM_EXTENT_X,SRD_PARAM_EXTENT_Z},
      {3.0f,4.0f,6.0f,6.0f} },
    { 1,  SRD_SYM_R, 1, {{SRD_COND_ADJACENT_HAS,SRD_SYM_B,0}},
      SRD_ACT_ROOM, 4,
      {SRD_PARAM_WIDTH,SRD_PARAM_HEIGHT,SRD_PARAM_EXTENT_X,SRD_PARAM_EXTENT_Z},
      {2.0f,4.0f,5.0f,5.0f} },
    { 2,  SRD_SYM_R, 1, {{SRD_COND_CELL_COUNT_GE,0,6}},
      SRD_ACT_SUB_REGION, 1,
      {SRD_PARAM_FRAC}, {0.5f} },

    /* B: Bar */
    { 3,  SRD_SYM_B, 0, {{0}}, SRD_ACT_ROOM, 4,
      {SRD_PARAM_WIDTH,SRD_PARAM_HEIGHT,SRD_PARAM_EXTENT_X,SRD_PARAM_EXTENT_Z},
      {3.0f,4.0f,5.0f,5.0f} },
    { 4,  SRD_SYM_B, 1, {{SRD_COND_ADJACENT_HAS,SRD_SYM_R,0}},
      SRD_ACT_ROOM, 4,
      {SRD_PARAM_WIDTH,SRD_PARAM_HEIGHT,SRD_PARAM_EXTENT_X,SRD_PARAM_EXTENT_Z},
      {2.0f,4.0f,5.0f,5.0f} },

    /* G: Entrance */
    { 5,  SRD_SYM_G, 0, {{0}}, SRD_ACT_ROOM, 4,
      {SRD_PARAM_WIDTH,SRD_PARAM_HEIGHT,SRD_PARAM_EXTENT_X,SRD_PARAM_EXTENT_Z},
      {2.0f,4.0f,5.0f,5.0f} },
    { 6,  SRD_SYM_G, 1, {{SRD_COND_ADJACENT_HAS,SRD_SYM_R,0}},
      SRD_ACT_ROOM, 4,
      {SRD_PARAM_WIDTH,SRD_PARAM_HEIGHT,SRD_PARAM_EXTENT_X,SRD_PARAM_EXTENT_Z},
      {2.0f,4.0f,5.0f,5.0f} },

    /* P: Private */
    { 7,  SRD_SYM_P, 0, {{0}}, SRD_ACT_ROOM, 4,
      {SRD_PARAM_WIDTH,SRD_PARAM_HEIGHT,SRD_PARAM_EXTENT_X,SRD_PARAM_EXTENT_Z},
      {1.0f,3.0f,4.0f,4.0f} },
    { 8,  SRD_SYM_P, 1, {{SRD_COND_ADJACENT_COUNT_LE,0,1}},
      SRD_ACT_ROOM, 4,
      {SRD_PARAM_WIDTH,SRD_PARAM_HEIGHT,SRD_PARAM_EXTENT_X,SRD_PARAM_EXTENT_Z},
      {1.0f,3.0f,3.0f,3.0f} },

    /* ^ / v: Stair anchors */
    { 9,  SRD_SYM_UP, 0, {{0}}, SRD_ACT_STAIR_STEP, 2,
      {SRD_PARAM_STEP_H,SRD_PARAM_STEP_D}, {0.25f,0.5f} },
    { 10, SRD_SYM_DN, 0, {{0}}, SRD_ACT_STAIR_STEP, 2,
      {SRD_PARAM_STEP_H,SRD_PARAM_STEP_D}, {0.25f,0.5f} },

    /* Connection rule: any two adjacent regions → corridor */
    { 11, SRD_SYM_R, 1, {{SRD_COND_ADJACENT_COUNT_GE,0,1}},
      SRD_ACT_CONNECT, 1,
      {SRD_PARAM_WIDTH}, {2.0f} },

    /* W: Wall between two rooms → doorway (reachability) */
    { 12, SRD_SYM_W, 2, {{SRD_COND_ADJACENT_COUNT_GE,0,2},
                          {SRD_COND_ADJACENT_COUNT_LE,0,2}},
      SRD_ACT_OPENING, 1,
      {SRD_PARAM_WIDTH}, {1.0f} },
};

static const int N_RULES = sizeof(RULE_TABLE) / sizeof(RULE_TABLE[0]);

int srd_rule_count(void) { return N_RULES; }
const srd_rule_t *srd_rule_table(void) { return RULE_TABLE; }

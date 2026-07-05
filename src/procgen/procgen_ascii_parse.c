#include "ferrum/procgen/procgen_ascii_parse.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FLOORS   8
#define MAX_ROWS     64
#define MAX_COLS     128
#define MAX_NODES    256
#define MAX_EDGES    (MAX_NODES * 4)

typedef struct {
    int    rows;
    int    cols;
    char   cells[MAX_ROWS][MAX_COLS];
    int    label[MAX_ROWS][MAX_COLS];   /* flood-fill region id, -1 = unvisited */
    int    floor_number;
    char   floor_name[64];
} floor_grid_t;

typedef struct {
    int   count;
    int   floor;      /* floor index this node belongs to */
    int   first_r;
    int   first_c;
    int   cell_count;
    char  type_char;
    bool  is_stair;   /* true if this is a stair anchor, not a room */
    int   stair_dir;  /* 0=up, 1=down */
    char  label[64];
} region_t;

typedef struct {
    int  a, b;
} edge_t;

typedef struct {
    int  room_node;
    int  stair_up_node;
    int  stair_down_node;
} stair_pair_t;

/* ── Internal helpers ────────────────────────────────────────────── */

static int parse_floor_header(const char *line, int *out_floor_num, char *out_name, size_t name_cap) {
    (void)name_cap;
    int num = 0;
    if (sscanf(line, "=== FLOOR %d: %[^\n]", &num, out_name) >= 1) {
        *out_floor_num = num;
        return 0;
    }
    /* Allow format without name: "=== FLOOR 0 ===" */
    if (sscanf(line, "=== FLOOR %d ===", &num) == 1) {
        *out_floor_num = num;
        out_name[0] = '\0';
        return 0;
    }
    /* Allow lowercase: "=== Floor 0:" */
    if (sscanf(line, "=== Floor %d: %[^\n]", &num, out_name) >= 1) {
        *out_floor_num = num;
        return 0;
    }
    return -1;
}

static bool is_room_char(char c) {
    return (c >= 'A' && c <= 'Z') && c != 'W';
}

static bool is_stair_char(char c) {
    return c == '^' || c == 'v';
}

/* ── Flood fill ─────────────────────────────────────────────────── */

static void flood_fill(floor_grid_t *fg, int r, int c, int region_id, char seed_char) {
    if (r < 0 || r >= fg->rows || c < 0 || c >= fg->cols) return;
    if (fg->label[r][c] != -1) return;

    char ch = fg->cells[r][c];
    if (ch != seed_char && ch != '.') return;

    /* Mark all same-char + dot cells as the same region */
    fg->label[r][c] = region_id;
    flood_fill(fg, r-1, c, region_id, seed_char);
    flood_fill(fg, r+1, c, region_id, seed_char);
    flood_fill(fg, r, c-1, region_id, seed_char);
    flood_fill(fg, r, c+1, region_id, seed_char);
}

static void absorb_dots(floor_grid_t *fg, int r, int c, int target_id) {
    if (r < 0 || r >= fg->rows || c < 0 || c >= fg->cols) return;
    if (fg->label[r][c] != -1 && fg->label[r][c] != -2) return;
    if (fg->cells[r][c] != '.') return;

    fg->label[r][c] = target_id;
    absorb_dots(fg, r-1, c, target_id);
    absorb_dots(fg, r+1, c, target_id);
    absorb_dots(fg, r, c-1, target_id);
    absorb_dots(fg, r, c+1, target_id);
}

/* ── Public API ──────────────────────────────────────────────────── */

int procgen_ascii_parse(const char *ascii, fr_room_graph_t *graph) {
    if (!ascii || !graph) return -1;

    fr_room_graph_init(graph);

    floor_grid_t floors[MAX_FLOORS];
    int n_floors = 0;
    memset(floors, 0, sizeof(floors));

    /* ── Pass 1: split into floor blocks ─────────────────────── */
    const char *p = ascii;
    while (*p) {
        /* Skip whitespace between blocks */
        while (*p == '\n' || *p == '\r') p++;
        if (*p == '\0') break;

        /* Must start with floor header */
        int floor_num;
        char floor_name[64];
        if (parse_floor_header(p, &floor_num, floor_name, sizeof(floor_name)) != 0)
            return -1;

        /* Advance past header line */
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;

        if (n_floors >= MAX_FLOORS) return -1;
        floor_grid_t *fg = &floors[n_floors];
        fg->floor_number = floor_num;
        strncpy(fg->floor_name, floor_name, sizeof(fg->floor_name) - 1);

        /* Read grid lines until next === header or end */
        int row = 0;
        while (*p && !(*p == '=' && *(p+1) == '=' && *(p+2) == '=')) {
            /* Parse one row: space-separated or character-joined chars */
            int col = 0;
            const char *line_start = p;

            /* Find end of line */
            while (*p && *p != '\n' && *p != '\r') p++;

            /* Tokenise: extract non-space characters */
            const char *s = line_start;
            while (s < p && col < MAX_COLS) {
                if (*s != ' ') {
                    fg->cells[row][col] = *s;
                    col++;
                }
                s++;
            }
            if (col > fg->cols) fg->cols = col;

            /* Skip newlines */
            while (*p == '\n' || *p == '\r') p++;

            row++;
            if (row >= MAX_ROWS) break;
        }
        fg->rows = row;
        n_floors++;
    }

    if (n_floors == 0) return -1;

    /* ── Pass 2: flood-fill to find regions ──────────────────── */
    region_t  regions[MAX_NODES];
    int       n_regions = 0;
    edge_t    edges[MAX_EDGES];
    int       n_edges = 0;
    stair_pair_t stair_pairs[MAX_FLOORS * 2];
    int       n_stair_pairs = 0;

    for (int fi = 0; fi < n_floors; fi++) {
        floor_grid_t *fg = &floors[fi];

        /* Init labels */
        for (int r = 0; r < fg->rows; r++)
            for (int c = 0; c < fg->cols; c++)
                fg->label[r][c] = -1;

        /* Mark walls as visited */
        for (int r = 0; r < fg->rows; r++)
            for (int c = 0; c < fg->cols; c++)
                if (fg->cells[r][c] == 'W')
                    fg->label[r][c] = -2; /* blocked */

        /* Flood-fill room regions */
        for (int r = 0; r < fg->rows; r++) {
            for (int c = 0; c < fg->cols; c++) {
                if (fg->label[r][c] != -1) continue;
                char ch = fg->cells[r][c];
                if (!is_room_char(ch) && !is_stair_char(ch)) continue;

                int rid = n_regions++;
                if (rid >= MAX_NODES) goto done;

                flood_fill(fg, r, c, rid, ch);

                /* After flood fill, merge dot cells adjacent to this region */
                for (int dr = 0; dr < fg->rows; dr++)
                    for (int dc = 0; dc < fg->cols; dc++)
                        if (fg->label[dr][dc] == rid)
                            absorb_dots(fg, dr, dc, rid);

                /* Record region */
                regions[rid].floor     = fi;
                regions[rid].first_r   = r;
                regions[rid].first_c   = c;
                regions[rid].type_char = ch;
                regions[rid].is_stair  = is_stair_char(ch);
                regions[rid].stair_dir = (ch == 'v') ? FR_STAIR_DOWN : FR_STAIR_UP;
                regions[rid].cell_count = 0;
                regions[rid].label[0]  = '\0';

                /* Count cells and find label hints from comments */
                for (int dr = 0; dr < fg->rows; dr++) {
                    for (int dc = 0; dc < fg->cols; dc++) {
                        if (fg->label[dr][dc] == rid)
                            regions[rid].cell_count++;
                    }
                }
            }
        }

        /* ── Extract adjacency edges ────────────────────────── */
        for (int r = 0; r < fg->rows; r++) {
            for (int c = 0; c < fg->cols; c++) {
                int lid = fg->label[r][c];
                if (lid < 0) continue; /* wall or unvisited */

                /* Check right neighbor */
                if (c + 1 < fg->cols) {
                    int rid = fg->label[r][c+1];
                    if (rid >= 0 && rid != lid
                        && !regions[lid].is_stair
                        && !regions[rid].is_stair) {
                        edges[n_edges++] = (edge_t){ lid, rid };
                        if (n_edges >= MAX_EDGES) goto done;
                    }
                }
                /* Check down neighbor */
                if (r + 1 < fg->rows) {
                    int did = fg->label[r+1][c];
                    if (did >= 0 && did != lid
                        && !regions[lid].is_stair
                        && !regions[did].is_stair) {
                        edges[n_edges++] = (edge_t){ lid, did };
                        if (n_edges >= MAX_EDGES) goto done;
                    }
                }
            }
        }

        /* ── Link stair anchors to adjacent rooms ────────────── */
        for (int ri = 0; ri < n_regions; ri++) {
            if (!regions[ri].is_stair) continue;
            if (regions[ri].floor != fi) continue;

            /* Find adjacent room */
            int sr = regions[ri].first_r;
            int sc = regions[ri].first_c;
            int adjacent_room = -1;

            static const int dr4[] = {-1, 1, 0, 0};
            static const int dc4[] = {0, 0, -1, 1};
            for (int d = 0; d < 4; d++) {
                int nr = sr + dr4[d];
                int nc = sc + dc4[d];
                if (nr >= 0 && nr < fg->rows && nc >= 0 && nc < fg->cols) {
                    int nid = fg->label[nr][nc];
                    if (nid >= 0 && !regions[nid].is_stair) {
                        adjacent_room = nid;
                        break;
                    }
                }
            }
            if (adjacent_room >= 0) {
                regions[ri].is_stair = true; /* already marked */
                /* Record as edge from stair to its room */
                edges[n_edges++] = (edge_t){ ri, adjacent_room };
                if (n_edges >= MAX_EDGES) goto done;
            }
        }
    }

    /* ── Pass 3: match stair pairs across floors ───────────── */
    {
        int up_ids[MAX_FLOORS * 4];
        int down_ids[MAX_FLOORS * 4];
        int n_up = 0, n_down = 0;

        for (int ri = 0; ri < n_regions; ri++) {
            if (!regions[ri].is_stair) continue;
            if (regions[ri].stair_dir == FR_STAIR_UP) {
                up_ids[n_up] = ri;
                n_up++;
            } else {
                down_ids[n_down] = ri;
                n_down++;
            }
        }

        /* Match each up-stair on floor N with closest down-stair on floor N+1 */
        for (int ui = 0; ui < n_up; ui++) {
            int best = -1;
            int best_dist = 999999;
            int ufloor = regions[up_ids[ui]].floor;

            for (int di = 0; di < n_down; di++) {
                int dfloor = regions[down_ids[di]].floor;
                if (dfloor == ufloor + 1) {
                    /* Simple match: just pair them */
                    int dr = regions[down_ids[di]].first_r;
                    int dc = regions[down_ids[di]].first_c;
                    int ur = regions[up_ids[ui]].first_r;
                    int uc = regions[up_ids[ui]].first_c;
                    int dist = (dr - ur)*(dr - ur) + (dc - uc)*(dc - uc);
                    if (dist < best_dist) {
                        best_dist = dist;
                        best = di;
                    }
                }
            }

            if (best >= 0) {
                stair_pairs[n_stair_pairs].room_node      = -1; /* filled below */
                stair_pairs[n_stair_pairs].stair_up_node   = up_ids[ui];
                stair_pairs[n_stair_pairs].stair_down_node = down_ids[best];
                n_stair_pairs++;
            }
        }
    }

done:
    /* ── Pass 4: build output graph ─────────────────────────── */
    graph->node_count       = n_regions;
    graph->nodes            = n_regions > 0
                              ? (fr_graph_node_t *)calloc(n_regions, sizeof(fr_graph_node_t))
                              : NULL;

    int room_index = 0;
    int stair_index = 0;
    for (int i = 0; i < n_regions; i++) {
        if (regions[i].is_stair) {
            graph->nodes[i].type       = FR_GRAPH_NODE_STAIR_ANCHOR;
            graph->nodes[i].type_char   = (regions[i].stair_dir == FR_STAIR_DOWN) ? 'v' : '^';
            graph->nodes[i].first_cell_x = regions[i].first_c;
            graph->nodes[i].first_cell_z = regions[i].first_r;
            graph->nodes[i].cell_count   = regions[i].cell_count;
            strncpy(graph->nodes[i].label, regions[i].label,
                    sizeof(graph->nodes[i].label) - 1);
        } else {
            graph->nodes[i].type       = FR_GRAPH_NODE_ROOM;
            graph->nodes[i].type_char   = regions[i].type_char;
            graph->nodes[i].first_cell_x = regions[i].first_c;
            graph->nodes[i].first_cell_z = regions[i].first_r;
            graph->nodes[i].cell_count   = regions[i].cell_count;
            strncpy(graph->nodes[i].label, regions[i].label,
                    sizeof(graph->nodes[i].label) - 1);
        }
    }

    /* Deduplicate edges (undirected) */
    int unique_edges = 0;
    for (int i = 0; i < n_edges; i++) {
        bool dup = false;
        for (int j = 0; j < i; j++) {
            if ((edges[i].a == edges[j].a && edges[i].b == edges[j].b)
                || (edges[i].a == edges[j].b && edges[i].b == edges[j].a)) {
                dup = true;
                break;
            }
        }
        if (!dup) unique_edges++;
    }

    graph->edge_count       = unique_edges;
    graph->edges            = unique_edges > 0
                              ? (fr_graph_edge_t *)calloc(unique_edges, sizeof(fr_graph_edge_t))
                              : NULL;

    int ei = 0;
    for (int i = 0; i < n_edges; i++) {
        bool dup = false;
        for (int j = 0; j < i; j++) {
            if ((edges[i].a == edges[j].a && edges[i].b == edges[j].b)
                || (edges[i].a == edges[j].b && edges[i].b == edges[j].a)) {
                dup = true;
                break;
            }
        }
        if (!dup && ei < (int)graph->edge_count) {
            graph->edges[ei].node_a = edges[i].a;
            graph->edges[ei].node_b = edges[i].b;
            ei++;
        }
    }

    /* ── Stair pairs ────────────────────────────────────────── */
    graph->stair_pair_count = n_stair_pairs;
    graph->stair_pairs      = n_stair_pairs > 0
                              ? (fr_graph_stair_pair_t *)calloc(n_stair_pairs,
                                    sizeof(fr_graph_stair_pair_t))
                              : NULL;

    for (int i = 0; i < n_stair_pairs; i++) {
        graph->stair_pairs[i].room_node        = stair_pairs[i].room_node;
        graph->stair_pairs[i].stair_anchor_node = stair_pairs[i].stair_up_node;
    }

    (void)room_index;
    (void)stair_index;

    return 0;
}

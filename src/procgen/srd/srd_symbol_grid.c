#include "ferrum/procgen/srd/srd_symbol_grid.h"
#include <string.h>
#include <stdbool.h>

int srd_symbol_grid_parse(const char **lines, int n_lines,
                          srd_symbol_grid_t *grid) {
    if (!lines || !grid || n_lines < 1) return -1;
    memset(grid, 0, sizeof(*grid));

    /* Parse grid dimensions */
    grid->height = n_lines;
    grid->width  = 0;
    for (int z = 0; z < n_lines; z++) {
        int w = 0;
        for (const char *p = lines[z]; *p; p++)
            if (*p != ' ') { grid->cells[z][w] = *p; w++; }
        if (w > grid->width) grid->width = w;
    }

    /* Init region IDs to -1 */
    for (int z = 0; z < grid->height; z++)
        for (int x = 0; x < grid->width; x++)
            grid->region_id[z][x] = -1;

    /* Flood-fill: group connected same-char cells into regions */
    int next_id = 0;
    for (int z = 0; z < grid->height; z++) {
        for (int x = 0; x < grid->width; x++) {
            if (grid->region_id[z][x] != -1) continue;
            char ch = grid->cells[z][x];
            if (ch == 'W' || ch == '.' || ch == '^' || ch == 'v') continue;

            /* New region */
            int rid = next_id++;
            static int stack_x[SRD_MAX_WIDTH * SRD_MAX_HEIGHT];
            static int stack_z[SRD_MAX_WIDTH * SRD_MAX_HEIGHT];
            int sp = 0;
            stack_x[sp] = x; stack_z[sp] = z; sp++;
            grid->region_id[z][x] = rid;

            int cell_count = 0;
            while (sp > 0) {
                sp--;
                int cx = stack_x[sp], cz = stack_z[sp];
                cell_count++;

                static const int dx[4] = {-1, 1, 0, 0};
                static const int dz[4] = {0, 0, -1, 1};
                for (int d = 0; d < 4; d++) {
                    int nx = cx + dx[d], nz = cz + dz[d];
                    if (nx < 0 || nx >= grid->width || nz < 0 || nz >= grid->height) continue;
                    if (grid->region_id[nz][nx] != -1) continue;
                    char nc = grid->cells[nz][nx];
                    if (nc != ch && nc != '.') continue;
                    grid->region_id[nz][nx] = rid;
                    stack_x[sp] = nx; stack_z[sp] = nz; sp++;
                }
            }

            grid->regions[rid].id         = rid;
            grid->regions[rid].type_char  = ch;
            grid->regions[rid].cell_count = cell_count;
            grid->regions[rid].first_cell.x = x;
            grid->regions[rid].first_cell.z = z;
            grid->regions[rid].label[0]   = '\0';
            grid->n_regions = rid + 1;
        }
    }

    /* Extract adjacency edges between different regions */
    int n_edges = 0;
    for (int z = 0; z < grid->height - 1; z++) {
        for (int x = 0; x < grid->width - 1; x++) {
            int ra = grid->region_id[z][x];
            int rb_r = grid->region_id[z][x+1];
            int rb_d = grid->region_id[z+1][x];
            if (ra >= 0 && rb_r >= 0 && ra != rb_r) {
                /* Check not duplicate */
                bool dup = false;
                for (int e = 0; e < n_edges; e++)
                    if ((grid->edges[e].a == ra && grid->edges[e].b == rb_r)
                     || (grid->edges[e].a == rb_r && grid->edges[e].b == ra))
                        { dup = true; break; }
                if (!dup) { grid->edges[n_edges].a = ra; grid->edges[n_edges].b = rb_r; n_edges++; }
            }
            if (ra >= 0 && rb_d >= 0 && ra != rb_d) {
                bool dup = false;
                for (int e = 0; e < n_edges; e++)
                    if ((grid->edges[e].a == ra && grid->edges[e].b == rb_d)
                     || (grid->edges[e].a == rb_d && grid->edges[e].b == ra))
                        { dup = true; break; }
                if (!dup) { grid->edges[n_edges].a = ra; grid->edges[n_edges].b = rb_d; n_edges++; }
            }
        }
    }
    grid->n_edges = n_edges;

    return 0;
}

int srd_symbol_grid_region_at(const srd_symbol_grid_t *grid, int x, int z) {
    if (!grid || x < 0 || x >= grid->width || z < 0 || z >= grid->height) return -1;
    return grid->region_id[z][x];
}

int srd_symbol_grid_adjacent_count(const srd_symbol_grid_t *grid, int rid) {
    if (!grid || rid < 0) return 0;
    int count = 0;
    for (int e = 0; e < grid->n_edges; e++)
        if (grid->edges[e].a == rid || grid->edges[e].b == rid) count++;
    return count;
}

int srd_symbol_grid_adjacent_regions(const srd_symbol_grid_t *grid,
                                      int rid, int *out, int max_out) {
    if (!grid || rid < 0 || !out) return 0;
    int count = 0;
    for (int e = 0; e < grid->n_edges && count < max_out; e++) {
        if (grid->edges[e].a == rid) out[count++] = grid->edges[e].b;
        else if (grid->edges[e].b == rid) out[count++] = grid->edges[e].a;
    }
    return count;
}

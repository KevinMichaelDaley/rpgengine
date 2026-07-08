#ifndef FERRUM_PROCGEN_SRD_SYMBOL_GRID_H
#define FERRUM_PROCGEN_SRD_SYMBOL_GRID_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SRD_MAX_WIDTH  128
#define SRD_MAX_HEIGHT 64
#define SRD_MAX_REGIONS 256

typedef struct {
    int x, z;
} srd_cell_t;

typedef struct {
    int      id;
    char     type_char;
    int      cell_count;
    srd_cell_t first_cell;
    char     label[64];
} srd_region_t;

typedef struct {
    int   width, height;
    char  cells[SRD_MAX_HEIGHT][SRD_MAX_WIDTH];
    int   region_id[SRD_MAX_HEIGHT][SRD_MAX_WIDTH];
    int   n_regions;
    srd_region_t regions[SRD_MAX_REGIONS];
    int   n_edges;
    struct { int a, b; } edges[SRD_MAX_REGIONS * 4];
} srd_symbol_grid_t;

/**
 * @brief Parse a single-floor ASCII grid into a symbol grid.
 */
int srd_symbol_grid_parse(const char **lines, int n_lines,
                          srd_symbol_grid_t *grid);

/**
 * @brief Get region id for a cell position.
 */
int srd_symbol_grid_region_at(const srd_symbol_grid_t *grid, int x, int z);

/**
 * @brief Count distinct region types adjacent to a region.
 */
int srd_symbol_grid_adjacent_count(const srd_symbol_grid_t *grid,
                                    int region_id);

/**
 * @brief Get a list of adjacent region IDs for a given region.
 * Returns number of adjacent regions (up to 4).
 */
int srd_symbol_grid_adjacent_regions(const srd_symbol_grid_t *grid,
                                      int region_id,
                                      int *out_regions, int max_out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROCGEN_SRD_SYMBOL_GRID_H */

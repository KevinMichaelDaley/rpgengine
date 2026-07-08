#include "ferrum/procgen/srd/srd_tile.h"
#include <string.h>

void srd_tile_list_init(srd_tile_list_t *list) {
    memset(list, 0, sizeof(*list));
    list->cell_scale = 2.0f;  /* default: 2m per grid cell */
}

static void add_tile(srd_tile_list_t *list,
                      srd_tile_type_t type,
                      int x, int z,
                      float floor_h, float ceil_h) {
    if (list->count >= SRD_MAX_TILES) return;
    srd_tile_t *t = &list->tiles[list->count++];
    t->type    = type;
    t->grid_x  = x;
    t->grid_z  = z;
    t->floor_z = floor_h;
    t->ceil_z  = ceil_h;
}

void srd_tile_add_region(srd_tile_list_t *list,
                          int grid_x, int grid_z,
                          int width, int depth,
                          int has_adjacent,
                          float floor_h, float ceil_h) {
    /* Floor: every interior cell */
    for (int z = 0; z < depth; z++) {
        for (int x = 0; x < width; x++) {
            add_tile(list, SRD_TILE_FLOOR,
                     grid_x + x, grid_z + z,
                     floor_h, floor_h + 0.25f);
        }
    }

    /* Ceiling: every interior cell */
    for (int z = 0; z < depth; z++) {
        for (int x = 0; x < width; x++) {
            add_tile(list, SRD_TILE_CEILING,
                     grid_x + x, grid_z + z,
                     ceil_h - 0.25f, ceil_h);
        }
    }

    /* Walls: perimeter cells */
    int north_wall = !(has_adjacent & 0x1);  /* no adjacent region to north */
    int south_wall = !(has_adjacent & 0x2);
    int east_wall  = !(has_adjacent & 0x4);
    int west_wall  = !(has_adjacent & 0x8);

    /* North wall */
    if (north_wall) {
        for (int x = 0; x < width; x++) {
            add_tile(list, SRD_TILE_WALL,
                     grid_x + x, grid_z,
                     floor_h, ceil_h);
        }
    } else {
        /* Doorway: center void tile in the wall */
        int door_x = grid_x + width / 2;
        for (int x = 0; x < width; x++) {
            int xx = grid_x + x;
            if (xx == door_x || (width <= 4 && (xx == door_x - 1 || xx == door_x + 1))) {
                add_tile(list, SRD_TILE_VOID, xx, grid_z, floor_h, ceil_h);
            } else {
                add_tile(list, SRD_TILE_WALL, xx, grid_z, floor_h, ceil_h);
            }
        }
    }

    /* South wall */
    if (south_wall) {
        for (int x = 0; x < width; x++) {
            add_tile(list, SRD_TILE_WALL,
                     grid_x + x, grid_z + depth - 1,
                     floor_h, ceil_h);
        }
    } else {
        int door_x = grid_x + width / 2;
        for (int x = 0; x < width; x++) {
            int xx = grid_x + x;
            if (xx == door_x || (width <= 4 && (xx == door_x - 1 || xx == door_x + 1))) {
                add_tile(list, SRD_TILE_VOID, xx, grid_z + depth - 1, floor_h, ceil_h);
            } else {
                add_tile(list, SRD_TILE_WALL, xx, grid_z + depth - 1, floor_h, ceil_h);
            }
        }
    }

    /* East wall */
    if (east_wall) {
        for (int z = 1; z < depth - 1; z++) {
            add_tile(list, SRD_TILE_WALL,
                     grid_x + width - 1, grid_z + z,
                     floor_h, ceil_h);
        }
    } else {
        int door_z = grid_z + depth / 2;
        for (int z = 1; z < depth - 1; z++) {
            int zz = grid_z + z;
            if (zz == door_z || (depth <= 4 && (zz == door_z - 1 || zz == door_z + 1))) {
                add_tile(list, SRD_TILE_VOID, grid_x + width - 1, zz, floor_h, ceil_h);
            } else {
                add_tile(list, SRD_TILE_WALL, grid_x + width - 1, zz, floor_h, ceil_h);
            }
        }
    }

    /* West wall */
    if (west_wall) {
        for (int z = 1; z < depth - 1; z++) {
            add_tile(list, SRD_TILE_WALL,
                     grid_x, grid_z + z,
                     floor_h, ceil_h);
        }
    } else {
        int door_z = grid_z + depth / 2;
        for (int z = 1; z < depth - 1; z++) {
            int zz = grid_z + z;
            if (zz == door_z || (depth <= 4 && (zz == door_z - 1 || zz == door_z + 1))) {
                add_tile(list, SRD_TILE_VOID, grid_x, zz, floor_h, ceil_h);
            } else {
                add_tile(list, SRD_TILE_WALL, grid_x, zz, floor_h, ceil_h);
            }
        }
    }
}

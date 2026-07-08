/**
 * @file srd_tile_rasterize.c
 * @brief Rasterize a list of individual tiles into an SVO octree.
 */
#include "ferrum/npc/npc_svo.h"
#include "ferrum/procgen/procgen_svo_builder.h"
#include "ferrum/procgen/srd/srd_tile.h"

#include <stdlib.h>
#include <string.h>

void srd_tile_list_init(srd_tile_list_t *list) {
  list->count = 0;
  list->cap = 4096;
  list->tiles = (srd_tile_t *)calloc(list->cap, sizeof(srd_tile_t));
}

void srd_tile_list_free(srd_tile_list_t *list) {
  free(list->tiles);
  memset(list, 0, sizeof(*list));
}

void srd_tile_list_add(srd_tile_list_t *list, srd_tile_type_t type, float x,
                       float z, float floor_h, float ceil_h, float half_x,
                       float half_z) {
  if (list->count >= list->cap) {
    int newcap = list->cap * 2;
    srd_tile_t *nd =
        (srd_tile_t *)realloc(list->tiles, newcap * sizeof(srd_tile_t));
    if (!nd)
      return;
    list->tiles = nd;
    list->cap = newcap;
  }
  srd_tile_t *t = &list->tiles[list->count++];
  t->type = type;
  t->x = x;
  t->z = z;
  t->floor_z = floor_h;
  t->ceil_z = ceil_h;
  t->half_x = half_x > 0 ? half_x : 1.0f;
  t->half_z = half_z > 0 ? half_z : 1.0f;
}

/* ── Octree marking (same logic as svo_mark_block) ──────────── */

static void mark_voxel(npc_svo_grid_t *grid, int vx, int vy, int vz,
                       uint16_t material) {
  if (!grid || !grid->nodes)
    return;
  uint32_t max_coord = 1u << grid->max_depth;
  if (vx < 0 || vy < 0 || vz < 0 || (uint32_t)vx >= max_coord ||
      (uint32_t)vy >= max_coord || (uint32_t)vz >= max_coord)
    return;

  uint32_t node_index = 0;
  uint32_t cell_count = max_coord;
  int depth = 0;

  for (depth = 0; depth < (int)grid->max_depth; depth++) {
    cell_count >>= 1;
    uint32_t cx = ((uint32_t)vz / cell_count) & 1;
    uint32_t cy = ((uint32_t)vy / cell_count) & 1;
    uint32_t cxx = ((uint32_t)vx / cell_count) & 1;
    uint32_t child_idx = (cx << 2) | (cy << 1) | cxx;

    npc_svo_node_t *node = &grid->nodes[node_index];
    uint32_t child = node->children[child_idx];

    if (child == NPC_SVO_INVALID_NODE) {
      child = npc_svo_alloc_node(grid);
      if (child == NPC_SVO_INVALID_NODE)
        return;
      node->children[child_idx] = child;
      grid->nodes[child].parent = node_index;
      node->occupancy |= (1u << child_idx);
    }
    node_index = child;
  }

  grid->nodes[node_index].flags |= NPC_SVO_FLAG_SOLID;
  grid->nodes[node_index].material = material;
}

/* ── Tile rasterizer ─────────────────────────────────────────── */

uint32_t srd_tile_rasterize(npc_svo_grid_t *grid,
                            const srd_tile_list_t *tiles) {
  if (!grid || !tiles)
    return 0;

  uint32_t total = 0;
  int max_coord = (int)(1u << grid->max_depth);
  float vs = grid->voxel_size;
  float wx0 = grid->world_bounds.min.x;
  float wy0 = grid->world_bounds.min.y;
  float wz0 = grid->world_bounds.min.z;

  for (int ti = 0; ti < tiles->count; ti++) {
    const srd_tile_t *t = &tiles->tiles[ti];
    if (t->type == SRD_TILE_VOID)
      continue;

    float half_x = t->half_x > 0 ? t->half_x : 1.0f;
    float half_z = t->half_z > 0 ? t->half_z : 1.0f;

    int vx_min = (int)((t->x - half_x - wx0) / vs);
    int vx_max = (int)((t->x + half_x - wx0) / vs);
    int vz_min = (int)((t->z - half_z - wz0) / vs);
    int vz_max = (int)((t->z + half_z - wz0) / vs);
    int vy0 = (int)((t->floor_z - wy0) / vs);
    int vy1 = (int)((t->ceil_z - wy0) / vs);

    if (vx_min < 0)
      vx_min = 0;
    if (vx_max >= max_coord)
      vx_max = max_coord - 1;
    if (vz_min < 0)
      vz_min = 0;
    if (vz_max >= max_coord)
      vz_max = max_coord - 1;
    if (vy0 < 0)
      vy0 = 0;
    if (vy1 >= max_coord)
      vy1 = max_coord - 1;

    uint16_t mat =
        (t->type == SRD_TILE_FLOOR || t->type == SRD_TILE_CEILING) ? 1 : 2;

    switch (t->type) {
    case SRD_TILE_FLOOR:
    case SRD_TILE_CEILING:
      for (int vz = vz_min; vz <= vz_max; vz++)
        for (int vx = vx_min; vx <= vx_max; vx++) {
          mark_voxel(grid, vx, vy0, vz, mat);
          total++;
        }
      break;
    case SRD_TILE_WALL:
    case SRD_TILE_STAIR:
      for (int vz = vz_min; vz <= vz_max; vz++)
        for (int vx = vx_min; vx <= vx_max; vx++)
          for (int y = vy0; y <= vy1; y++) {
            mark_voxel(grid, vx, y, vz, mat);
            total++;
          }
      break;
    default:
      break;
    }
  }

  return total;
}

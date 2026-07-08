/**
 * @file srd_tile.h
 * @brief Terminal symbols: single tiles. Everything else is grammar.
 */
#ifndef FERRUM_PROCGEN_SRD_TILE_H
#define FERRUM_PROCGEN_SRD_TILE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SRD_MAX_TILES 65536

/* Forward declaration — full type defined in ferrum/npc/npc_svo.h */
typedef struct npc_svo_grid npc_svo_grid_t;

typedef enum {
  SRD_TILE_WALL = 0,    /* solid wall column from floor_z to ceil_z */
  SRD_TILE_FLOOR = 1,   /* solid floor block (1 cell thick) */
  SRD_TILE_CEILING = 2, /* solid ceiling block (1 cell thick) */
  SRD_TILE_VOID = 3,    /* explicitly empty — clears wall for doorway */
  SRD_TILE_STAIR = 4,   /* stair step block */
} srd_tile_type_t;

typedef struct {
  srd_tile_type_t type;
  float x, z;    /* world-space center position */
  float floor_z; /* bottom Y */
  float ceil_z;  /* top Y */
  float half_x;  /* half-extent in X */
  float half_z;  /* half-extent in Z */
} srd_tile_t;

typedef struct {
  srd_tile_t *tiles;
  int count;
  int cap;
} srd_tile_list_t;

void srd_tile_list_init(srd_tile_list_t *list);
void srd_tile_list_free(srd_tile_list_t *list);
void srd_tile_list_add(srd_tile_list_t *list, srd_tile_type_t type, float x,
                       float z, float floor_h, float ceil_h, float half_x,
                       float half_z);

/**
 * @brief Rasterize a tile list into an SVO grid.
 */
uint32_t srd_tile_rasterize(npc_svo_grid_t *grid, const srd_tile_list_t *tiles);

#ifdef __cplusplus
}
#endif
#endif

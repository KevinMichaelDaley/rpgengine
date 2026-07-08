#ifndef FERRUM_PROCGEN_SRD_GRAMMAR_H
#define FERRUM_PROCGEN_SRD_GRAMMAR_H

#include "ferrum/procgen/procgen_srd_types.h"
#include "ferrum/procgen/srd/srd_tile.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SRD_MAX_RULES 512
#define SRD_MAX_TREE_NODES 2048
#define SRD_MAX_SYMBOLS 256

typedef enum {
  SRD_SYM_R = 0,
  SRD_SYM_B = 1,
  SRD_SYM_G = 2,
  SRD_SYM_P = 3,
  SRD_SYM_W = 4,
  SRD_SYM_DOT = 5,
  SRD_SYM_UP = 6,
  SRD_SYM_DN = 7,
  SRD_SYM_WALL_SEG = 8,
  SRD_SYM_FLOOR_SEG = 9,
  SRD_SYM_CEILING_SEG = 10,
  SRD_SYM_CORRIDOR = 11,
  SRD_SYM_STAIR_ANCHOR = 12,
  SRD_SYM_WALL_WITH_DOOR = 13,
  /* terminals */
  SRD_SYM_WALL = 14,
  SRD_SYM_VOID = 15,
  SRD_SYM_FLOOR = 16,
  SRD_SYM_CEILING = 17,
  SRD_SYM_STAIR = 18,
} srd_symbol_t;

typedef enum {
  SRD_COND_NONE = 0,
  SRD_COND_ADJACENT_HAS,
  SRD_COND_ADJACENT_COUNT_GE,
  SRD_COND_CELL_COUNT_GE,
  SRD_COND_ADJACENT_COUNT_LE,
  SRD_COND_HAS_CHILDREN,
  SRD_COND_NO_CHILDREN,
  SRD_COND_WALL_HAS_DOOR,
  SRD_COND_HAS_FLAG,
  SRD_COND_NO_FLAG,
  SRD_COND_HAS_CONNECT,
  SRD_COND_NO_CONNECT,
} srd_cond_type_t;

typedef struct {
  srd_cond_type_t type;
  srd_symbol_t sym;
  int ival;
} srd_condition_t;

typedef enum {
  SRD_PARAM_WIDTH = 0,
  SRD_PARAM_HEIGHT = 1,
  SRD_PARAM_EXTENT_X = 2,
  SRD_PARAM_EXTENT_Z = 3,
  SRD_PARAM_STEP_H = 4,
  SRD_PARAM_STEP_D = 5,
  SRD_PARAM_FRAC = 6,
  SRD_PARAM_COUNT = 7,
} srd_param_t;

typedef enum {
  SRD_ACT_ROOM = 0,
  SRD_ACT_CONNECT = 1,
  SRD_ACT_DISCONNECT = 2,
  SRD_ACT_MERGE_ROOM = 3,
  SRD_ACT_TO_DOOR = 4,
  SRD_ACT_TO_STAIR = 5,
  SRD_ACT_EMIT_WALL = 6,
  SRD_ACT_EMIT_VOID = 7,
  SRD_ACT_EMIT_FLOOR = 8,
  SRD_ACT_EMIT_CEILING = 9,
  SRD_ACT_EMIT_STAIR = 10,
  SRD_ACT_CORRIDOR = 11,
  SRD_ACT_MERGE_CORRIDOR = 12,
  SRD_ACT_STAIR_TERM = 13,
  SRD_ACT_STAIR_TERM_REV = 14,
} srd_action_t;

typedef struct {
  int id;
  srd_symbol_t symbol;
  int n_conditions;
  srd_condition_t conditions[4];
  srd_action_t action;
  int n_params;
  srd_param_t params[8];
  float defaults[8];
} srd_rule_t;

#define SRD_FLAG_ROOM (1 << 0)

typedef struct srd_tree_node {
  int id, rule_id, parent_id, child_ids[256], n_children;
  srd_symbol_t symbol;
  float params[8];
  int region_id;
  int connect_from, connect_to, connect_dir, stair_dir;
  int flags;
  int created_step;
} srd_tree_node_t;

typedef struct {
  srd_tree_node_t nodes[SRD_MAX_TREE_NODES];
  int n_nodes;
  double energy;
} srd_grammar_t;

typedef struct {
  int width, height;
  char *cells;
  int *region_ids, *labels;
  int n_regions;
  struct {
    int id;
    char type_char;
    int first_x, first_z, cell_count;
    char label[32];
  } regions[128];
  int n_edges;
  struct {
    int a, b;
  } edges[256];
} srd_grid_t;

bool srd_symbol_is_terminal(srd_symbol_t sym);

void srd_grammar_init(srd_grammar_t *g);
int srd_grammar_add_node(srd_grammar_t *g, srd_symbol_t sym, int parent,
                         int rule_id);

int srd_grid_parse(const char **lines, int n, srd_grid_t *grid);
int srd_grid_region_at(const srd_grid_t *g, int x, int z);
int srd_grid_adjacent_regions(const srd_grid_t *g, int rid, int *out, int max);
srd_symbol_t srd_grid_region_symbol(const srd_grid_t *g, int rid);

bool srd_rule_matches(const srd_rule_t *r, const srd_grid_t *g,
                      const srd_tree_node_t *n);
int srd_rule_find_all(const srd_grid_t *g, const srd_tree_node_t *n,
                      const srd_rule_t *rules, int nr, int *out, int max);
int srd_rule_apply_to_node(srd_grammar_t *gram, int node_id,
                           const srd_rule_t *rule, const srd_grid_t *grid);

int srd_grammar_extract(const srd_grammar_t *gram, fr_room_box_t **rooms,
                        uint32_t *n_rooms, fr_corridor_seg_t **corrs,
                        uint32_t *n_corrs);
int srd_grammar_collect_tiles(const srd_grammar_t *gram, srd_tile_list_t *tiles,
                              const srd_grid_t *grid, float floor_h,
                              float ceil_h);

int srd_rule_count(void);
const srd_rule_t *srd_rule_table(void);

#ifdef __cplusplus
}
#endif
#endif

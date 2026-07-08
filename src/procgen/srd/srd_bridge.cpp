#include "ferrum/procgen/srd/srd_bridge.h"
#include "ferrum/procgen/srd/srd_grammar.h"
#include "ferrum/procgen/srd/srd_loss_compiler.h"
#include "ferrum/procgen/srd/srd_optimizer.h"
#include "ferrum/procgen/srd/srd_tile.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

int srd_generate(const char *ascii, uint32_t seed, double budget,
                 fr_room_box_t **r_out, uint32_t *nr, fr_corridor_seg_t **c_out,
                 uint32_t *nc) {
  if (!ascii || !r_out || !nr || !c_out || !nc)
    return -1;
  *r_out = NULL;
  *nr = 0;
  *c_out = NULL;
  *nc = 0;
  if (seed)
    srand(seed);
  else
    srand((uint32_t)time(NULL));

  /* Parse ASCII into lines (strip === headers and LOSS: block) */
  const char *lines[64];
  int nl = 0;
  const char *p = ascii;
  const char *loss_block = NULL;
  while (*p) {
    while (*p == '\n' || *p == '\r')
      p++;
    if (!*p)
      break;
    if (*p == '=' && p[1] == '=' && p[2] == '=') {
      while (*p && *p != '\n')
        p++;
      if (*p == '\n')
        p++;
      continue;
    }
    if (*p == 'L' && !strncmp(p, "LOSS:", 5)) {
      loss_block = p;
      break;
    }
    const char *s = p;
    while (*p && *p != '\n' && *p != '\r')
      p++;
    int len = p - s;
    if (len > 127)
      len = 127;
    static char buf[64][128];
    memcpy(buf[nl], s, len);
    buf[nl][len] = 0;
    lines[nl] = buf[nl];
    nl++;
    while (*p == '\n' || *p == '\r')
      p++;
  }
  if (nl < 1)
    return -1;

  /* Parse grid */
  srd_grid_t grid;
  if (srd_grid_parse(lines, nl, &grid) != 0)
    return -1;

  /* Build grammar tree: every region becomes a non-terminal */
  srd_grammar_t *gram = (srd_grammar_t *)calloc(1, sizeof(srd_grammar_t));
  if (!gram) {
    free(grid.cells);
    free(grid.region_ids);
    free(grid.labels);
    return -1;
  }
  srd_grammar_init(gram);

  for (int ri = 0; ri < grid.n_regions; ri++) {
    srd_symbol_t sym = srd_grid_region_symbol(&grid, ri);
    int nid = srd_grammar_add_node(gram, sym, -1, -1);
    if (nid >= 0)
      gram->nodes[nid].region_id = ri;
  }

  /* Run SRD loop on the grammar tree */
  srd_optimize_config_t cfg;
  srd_optimize_config_default(&cfg);
  cfg.time_budget_s = budget > 0 ? budget : 30.0;
  cfg.max_steps = (int)(cfg.time_budget_s * 200);
  cfg.verbose = 1;

  /* Parse loss terms yourself (label resolution against grid) */
  srd_loss_term_t terms[SRD_MAX_TERMS];
  uint32_t nt = 0;
  if (loss_block) {
    const char *p = loss_block + 5;
    char line[256];
    while (*p && nt < SRD_MAX_TERMS) {
      while (*p && (*p == '\n' || *p == '\r' || *p == ' '))
        p++;
      if (*p == 0)
        break;
      size_t len = 0;
      while (*p && *p != '\n' && *p != '\r' && len < 255)
        line[len++] = *p++;
      line[len] = 0;
      if (len == 0)
        continue;

      memset(&terms[nt], 0, sizeof(terms[nt]));
      for (int i = 0; i < 4; i++)
        terms[nt].label_indices[i] = (uint32_t)-1;

      char name[64], a1[64], a2[64];
      a1[0] = a2[0] = 0;
      float val = 0;
      char op = 0;
      int np = sscanf(line, "%63[a-zA-Z_](%63[^)]) %c %f", name, a1, &op, &val);
      if (np < 2) {
        sscanf(line, "%63[a-zA-Z_](%63[^)])", name, a1);
      }

      /* Resolve a1 to region id */
      char *comma = strchr(a1, ',');
      if (comma) {
        *comma = 0;
        strcpy(a2, comma + 1);
      }
      if (!strcmp(a1, "all")) {
        terms[nt].all_rooms = 1;
        terms[nt].label_indices[0] = 0;
      } else
        for (int ri = 0; ri < (int)grid.n_regions; ri++) {
          if (a1[0] && grid.regions[ri].type_char == a1[0])
            terms[nt].label_indices[0] = (uint32_t)ri;
          if (a2[0] && grid.regions[ri].type_char == a2[0])
            terms[nt].label_indices[1] = (uint32_t)ri;
        }

      if (!strcmp(name, "PathDistance"))
        terms[nt].primitive = FR_LOSS_PATH_DISTANCE;
      else if (!strcmp(name, "LineOfSight"))
        terms[nt].primitive = FR_LOSS_LINE_OF_SIGHT;
      else if (!strcmp(name, "NonPenetration"))
        terms[nt].primitive = FR_LOSS_NON_PENETRATION;
      else if (!strcmp(name, "MinimumSize"))
        terms[nt].primitive = FR_LOSS_MINIMUM_SIZE;
      else
        continue;

      terms[nt].target_value = val;
      terms[nt].op = (op == '<' ? 1 : op == '>' ? 0 : 2);
      nt++;
    }
  }

  srd_optimize(NULL, NULL, 0, c_out, nc, terms, nt, &cfg, &grid, gram);

  /* Collect tiles from the grammar tree */
  srd_tile_list_t tiles;
  srd_tile_list_init(&tiles);

  float floor_h = 0.0f;
  float ceil_h  = 4.0f;

  srd_grammar_collect_tiles(gram, &tiles, &grid, floor_h, ceil_h);

  /* Return tiles via pointer hack (compat with old API) */
  *nr      = (uint32_t)(tiles.count);
  *r_out   = (fr_room_box_t *)(tiles.tiles);
  *c_out   = NULL;
  *nc      = 0;

  free(grid.cells);
  free(grid.region_ids);
  free(grid.labels);
  free(gram);
  return 0;
}

void srd_free_geometry(fr_room_box_t *rooms, fr_corridor_seg_t *corridors) {
  (void)corridors;
  free(rooms);
}

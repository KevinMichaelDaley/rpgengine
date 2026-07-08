#include "ferrum/procgen/srd/srd_grammar.h"
#include "ferrum/procgen/srd/srd_tile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Symbol helpers ──────────────────────────────────────────── */

bool srd_symbol_is_terminal(srd_symbol_t sym) { return sym >= SRD_SYM_WALL; }

static srd_symbol_t char_to_sym(char c) {
  switch (c) {
  case 'R':
  case 'r':
    return SRD_SYM_R;
  case 'B':
  case 'b':
    return SRD_SYM_B;
  case 'G':
  case 'g':
    return SRD_SYM_G;
  case 'P':
  case 'p':
    return SRD_SYM_P;
  case 'W':
  case 'w':
    return SRD_SYM_W;
  case '.':
    return SRD_SYM_DOT;
  case '^':
    return SRD_SYM_UP;
  case 'v':
    return SRD_SYM_DN;
  default:
    return SRD_SYM_W;
  }
}

/* ── Grammar tree ────────────────────────────────────────────── */

void srd_grammar_init(srd_grammar_t *g) { memset(g, 0, sizeof(*g)); }

int srd_grammar_add_node(srd_grammar_t *g, srd_symbol_t sym, int parent,
                         int rule_id) {
  if (!g || g->n_nodes >= SRD_MAX_TREE_NODES)
    return -1;
  int idx = g->n_nodes++;
  memset(&g->nodes[idx], 0, sizeof(g->nodes[idx]));
  g->nodes[idx].id = idx;
  g->nodes[idx].symbol = sym;
  g->nodes[idx].parent_id = parent;
  g->nodes[idx].rule_id = rule_id;
  g->nodes[idx].region_id = -1;
  g->nodes[idx].connect_to = -1;
  g->nodes[idx].created_step = 0;
  if (parent >= 0 && parent < g->n_nodes) {
    srd_tree_node_t *p = &g->nodes[parent];
    if (p->n_children < 256)
      p->child_ids[p->n_children++] = idx;
  }
  return idx;
}

/* ── 2D Grid parser ──────────────────────────────────────────── */

int srd_grid_parse(const char **lines, int n_lines, srd_grid_t *grid) {
  if (!lines || !grid || n_lines < 1)
    return -1;
  memset(grid, 0, sizeof(*grid));
  grid->height = n_lines;
  for (int z = 0; z < n_lines; z++) {
    int w = 0;
    for (const char *p = lines[z]; *p; p++)
      if (*p != ' ')
        w++;
    if (w > grid->width)
      grid->width = w;
  }
  int total = grid->width * grid->height;
  grid->cells = (char *)malloc(total);
  grid->region_ids = (int *)malloc(total * sizeof(int));
  grid->labels = (int *)malloc(total * sizeof(int));
  for (int z = 0; z < grid->height; z++) {
    int x = 0;
    for (const char *p = lines[z]; *p; p++)
      if (*p != ' ')
        grid->cells[z * grid->width + x++] = *p;
  }
  for (int i = 0; i < total; i++) {
    grid->region_ids[i] = -1;
    grid->labels[i] = -1;
  }

  /* Flood-fill connected same-char regions */
  int nreg = 0;
  for (int z = 0; z < grid->height; z++) {
    for (int x = 0; x < grid->width; x++) {
      if (grid->region_ids[z * grid->width + x] != -1)
        continue;
      char ch = grid->cells[z * grid->width + x];
      if (ch == 'W' || ch == '.' || ch == '^' || ch == 'v')
        continue;

      int rid = nreg++;
      static int sx[1024], sz[1024];
      int sp = 0;
      sx[sp] = x;
      sz[sp] = z;
      sp++;
      grid->region_ids[z * grid->width + x] = rid;
      int cell_count = 0;

      while (sp > 0) {
        sp--;
        int cx = sx[sp], cz = sz[sp];
        cell_count++;
        static const int dx[] = {-1, 1, 0, 0};
        static const int dz[] = {0, 0, -1, 1};
        for (int d = 0; d < 4; d++) {
          int nx = cx + dx[d];
          int nz = cz + dz[d];
          if (nx < 0 || nx >= grid->width || nz < 0 || nz >= grid->height)
            continue;
          if (grid->region_ids[nz * grid->width + nx] != -1)
            continue;
          char nc = grid->cells[nz * grid->width + nx];
          if (nc != ch && nc != '.' && nc != '^' && nc != 'v')
            continue;
          grid->region_ids[nz * grid->width + nx] = rid;
          sx[sp] = nx;
          sz[sp] = nz;
          sp++;
        }
      }
      grid->regions[rid].id = rid;
      grid->regions[rid].type_char = ch;
      grid->regions[rid].first_x = x;
      grid->regions[rid].first_z = z;
      grid->regions[rid].cell_count = cell_count;
      grid->regions[rid].label[0] = 0;
      grid->n_regions = nreg;
    }
  }

  /* Extract adjacency edges */
  int ne = 0;
  for (int z = 0; z < grid->height; z++) {
    for (int x = 0; x < grid->width; x++) {
      int a = grid->region_ids[z * grid->width + x];
      if (a < 0)
        continue;
      /* Right neighbor */
      if (x + 1 < grid->width) {
        int b = grid->region_ids[z * grid->width + x + 1];
        if (b >= 0 && a != b) {
          int dup = 0;
          for (int e = 0; e < ne; e++)
            if ((grid->edges[e].a == a && grid->edges[e].b == b) ||
                (grid->edges[e].a == b && grid->edges[e].b == a))
              dup = 1;
          if (!dup) {
            grid->edges[ne].a = a;
            grid->edges[ne].b = b;
            ne++;
          }
        }
      }
      /* Down neighbor */
      if (z + 1 < grid->height) {
        int b = grid->region_ids[(z + 1) * grid->width + x];
        if (b >= 0 && a != b) {
          int dup = 0;
          for (int e = 0; e < ne; e++)
            if ((grid->edges[e].a == a && grid->edges[e].b == b) ||
                (grid->edges[e].a == b && grid->edges[e].b == a))
              dup = 1;
          if (!dup) {
            grid->edges[ne].a = a;
            grid->edges[ne].b = b;
            ne++;
          }
        }
      }
    }
  }
  /* Connect stair pairs: rooms containing ^ cells to rooms containing v cells
   * at matching x positions. ^ and v are absorbed into adjacent rooms, so find
   * which region owns each stair cell and connect across floors. */
  for (int z1 = 0; z1 < grid->height && ne < 255; z1++) {
    for (int x1 = 0; x1 < grid->width && ne < 255; x1++) {
      if (grid->cells[z1 * grid->width + x1] != '^')
        continue;
      int ra = grid->region_ids[z1 * grid->width + x1];
      if (ra < 0) continue;
      /* Find matching v at same x column */
      for (int z2 = 0; z2 < grid->height; z2++) {
        if (grid->cells[z2 * grid->width + x1] != 'v')
          continue;
        int rb = grid->region_ids[z2 * grid->width + x1];
        if (rb < 0 || rb == ra) continue;
        /* Add edge if not duplicate */
        int dup = 0;
        for (int e = 0; e < ne; e++)
          if ((grid->edges[e].a == ra && grid->edges[e].b == rb) ||
              (grid->edges[e].a == rb && grid->edges[e].b == ra))
            dup = 1;
        if (!dup && ne < 256) {
          grid->edges[ne].a = ra;
          grid->edges[ne].b = rb;
          ne++;
        }
      }
    }
  }

  grid->n_edges = ne;
  return 0;
}

int srd_grid_region_at(const srd_grid_t *g, int x, int z) {
  if (!g || x < 0 || x >= g->width || z < 0 || z >= g->height)
    return -1;
  return g->region_ids[z * g->width + x];
}

int srd_grid_adjacent_regions(const srd_grid_t *g, int rid, int *out,
                              int max_out) {
  if (!g || rid < 0 || !out)
    return 0;
  int n = 0;
  for (int e = 0; e < g->n_edges && n < max_out; e++) {
    if (g->edges[e].a == rid)
      out[n++] = g->edges[e].b;
    else if (g->edges[e].b == rid)
      out[n++] = g->edges[e].a;
  }
  return n;
}

srd_symbol_t srd_grid_region_symbol(const srd_grid_t *g, int rid) {
  if (!g || rid < 0 || rid >= (int)g->n_regions)
    return SRD_SYM_W;
  return char_to_sym(g->regions[rid].type_char);
}

/* ── Rule matching ───────────────────────────────────────────── */

static int count_adjacent_of_type(const srd_grid_t *g, int rid,
                                  srd_symbol_t sym) {
  int adj[8], count = 0;
  int n = srd_grid_adjacent_regions(g, rid, adj, 8);
  for (int i = 0; i < n; i++)
    if (srd_grid_region_symbol(g, adj[i]) == sym)
      count++;
  return count;
}

bool srd_rule_matches(const srd_rule_t *rule, const srd_grid_t *grid,
                      const srd_tree_node_t *node) {
  if (!rule || !grid || !node)
    return false;
  if (rule->symbol != node->symbol)
    return false;

  int region_id = node->region_id;

  for (int i = 0; i < rule->n_conditions; i++) {
    const srd_condition_t *c = &rule->conditions[i];
    switch (c->type) {
    case SRD_COND_NONE:
      break;
    case SRD_COND_ADJACENT_HAS:
      if (count_adjacent_of_type(grid, region_id, c->sym) == 0)
        return false;
      break;
    case SRD_COND_ADJACENT_COUNT_GE: {
      int adj[8];
      if (srd_grid_adjacent_regions(grid, region_id, adj, 8) < c->ival)
        return false;
      break;
    }
    case SRD_COND_CELL_COUNT_GE:
      if (region_id < 0 || region_id >= (int)grid->n_regions ||
          grid->regions[region_id].cell_count < c->ival)
        return false;
      break;
    case SRD_COND_ADJACENT_COUNT_LE: {
      int adj[8];
      if (srd_grid_adjacent_regions(grid, region_id, adj, 8) > c->ival)
        return false;
      break;
    }
    case SRD_COND_WALL_HAS_DOOR: {
      /* Check if wall at node position borders a non-wall region */
      float gx = node->params[SRD_PARAM_EXTENT_X] / 2.0f;
      float gz = node->params[SRD_PARAM_EXTENT_Z] / 2.0f;
      int ix = (int)(gx + 0.5f);
      int iz = (int)(gz + 0.5f);
      bool has_door = false;
      int dirs[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
      for (int d = 0; d < 4 && !has_door; d++) {
        int nx = ix + dirs[d][0];
        int nz = iz + dirs[d][1];
        int nrid = srd_grid_region_at(grid, nx, nz);
        if (nrid >= 0 && nrid < (int)grid->n_regions &&
            grid->regions[nrid].type_char != 'W' &&
            grid->regions[nrid].type_char != '.')
          has_door = true;
      }
      if (!has_door)
        return false;
      break;
    }
    case SRD_COND_HAS_CHILDREN:
      if (node->n_children == 0)
        return false;
      break;
    case SRD_COND_NO_CHILDREN:
      if (node->n_children > 0)
        return false;
      break;
    }
  }
  return true;
}

int srd_rule_find_all(const srd_grid_t *grid, const srd_tree_node_t *node,
                      const srd_rule_t *rules, int n_rules, int *out_indices,
                      int max_out) {
  if (!grid || !out_indices)
    return 0;
  int count = 0;
  for (int i = 0; i < n_rules && count < max_out; i++)
    if (srd_rule_matches(&rules[i], grid, node))
      out_indices[count++] = i;
  return count;
}

/* ── Convert region bounding box from grid cells ─────────────── */

static void region_bounds(const srd_grid_t *grid, int ri, int *min_x,
                          int *max_x, int *min_z, int *max_z) {
  *min_x = 999;
  *max_x = -1;
  *min_z = 999;
  *max_z = -1;
  for (int z = 0; z < grid->height; z++) {
    for (int x = 0; x < grid->width; x++) {
      if (grid->region_ids[z * grid->width + x] != ri)
        continue;
      if (x < *min_x)
        *min_x = x;
      if (x > *max_x)
        *max_x = x;
      if (z < *min_z)
        *min_z = z;
      if (z > *max_z)
        *max_z = z;
    }
  }
}

/* ── Rule application — creates terminal tile children ───────── */

int srd_rule_apply_to_node(srd_grammar_t *gram, int node_id,
                           const srd_rule_t *rule, const srd_grid_t *grid) {
  if (!gram || node_id < 0 || !rule)
    return -1;

  srd_tree_node_t *n = &gram->nodes[node_id];

  int ri = n->region_id;
  if (ri < 0 || ri >= (int)grid->n_regions)
    return -1;

  int mnx, mxx, mnz, mxz;
  region_bounds(grid, ri, &mnx, &mxx, &mnz, &mxz);
  if (mnx > mxx)
    return -1;

  float cell_scale = 2.0f;

  switch (rule->action) {

  /* ── ROOM: emit sub-segment children (no door logic) ──────── */
  case SRD_ACT_ROOM: {
    for (int gz = mnz; gz <= mxz; gz++) {
      for (int gx = mnx; gx <= mxx; gx++) {
        if (grid->region_ids[gz * grid->width + gx] != ri)
          continue;
        float wx = (float)gx * cell_scale;
        float wz = (float)gz * cell_scale;

        int fid = srd_grammar_add_node(gram, SRD_SYM_FLOOR_SEG, node_id, -1);
        if (fid >= 0) {
          gram->nodes[fid].params[SRD_PARAM_EXTENT_X] = wx;
          gram->nodes[fid].params[SRD_PARAM_EXTENT_Z] = wz;
          gram->nodes[fid].params[SRD_PARAM_WIDTH] = 1.0f;
          gram->nodes[fid].params[SRD_PARAM_HEIGHT] = 1.0f;
        }
        int cid = srd_grammar_add_node(gram, SRD_SYM_CEILING_SEG, node_id, -1);
        if (cid >= 0) {
          gram->nodes[cid].params[SRD_PARAM_EXTENT_X] = wx;
          gram->nodes[cid].params[SRD_PARAM_EXTENT_Z] = wz;
          gram->nodes[cid].params[SRD_PARAM_WIDTH] = 1.0f;
          gram->nodes[cid].params[SRD_PARAM_HEIGHT] = 1.0f;
        }
      }
    }

    /* Perimeter walls — always emit WALL_SEG, door logic is in WALL_SEG rules
     */
    for (int gx = mnx; gx <= mxx; gx++) {
      float wx = (float)gx * cell_scale;
      int nid = srd_grammar_add_node(gram, SRD_SYM_WALL_SEG, node_id, -1);
      if (nid >= 0) {
        gram->nodes[nid].params[SRD_PARAM_EXTENT_X] = wx;
        gram->nodes[nid].params[SRD_PARAM_EXTENT_Z] = (float)mnz * cell_scale;
        gram->nodes[nid].params[SRD_PARAM_WIDTH] = 1.0f;
        gram->nodes[nid].params[SRD_PARAM_HEIGHT] = 1.0f;
      }
      int sid = srd_grammar_add_node(gram, SRD_SYM_WALL_SEG, node_id, -1);
      if (sid >= 0) {
        gram->nodes[sid].params[SRD_PARAM_EXTENT_X] = wx;
        gram->nodes[sid].params[SRD_PARAM_EXTENT_Z] = (float)mxz * cell_scale;
        gram->nodes[sid].params[SRD_PARAM_WIDTH] = 1.0f;
        gram->nodes[sid].params[SRD_PARAM_HEIGHT] = 1.0f;
      }
    }
    for (int gz = mnz + 1; gz < mxz; gz++) {
      float wz = (float)gz * cell_scale;
      int eid = srd_grammar_add_node(gram, SRD_SYM_WALL_SEG, node_id, -1);
      if (eid >= 0) {
        gram->nodes[eid].params[SRD_PARAM_EXTENT_X] = (float)mxx * cell_scale;
        gram->nodes[eid].params[SRD_PARAM_EXTENT_Z] = wz;
        gram->nodes[eid].params[SRD_PARAM_WIDTH] = 1.0f;
        gram->nodes[eid].params[SRD_PARAM_HEIGHT] = 1.0f;
      }
      int wid = srd_grammar_add_node(gram, SRD_SYM_WALL_SEG, node_id, -1);
      if (wid >= 0) {
        gram->nodes[wid].params[SRD_PARAM_EXTENT_X] = (float)mnx * cell_scale;
        gram->nodes[wid].params[SRD_PARAM_EXTENT_Z] = wz;
        gram->nodes[wid].params[SRD_PARAM_WIDTH] = 1.0f;
        gram->nodes[wid].params[SRD_PARAM_HEIGHT] = 1.0f;
      }
    }
    break;
  }

  /* ── CONNECT: set structural link between nodes ───────────── */
  case SRD_ACT_CONNECT: {
    int adj[8];
    int na = srd_grid_adjacent_regions(grid, ri, adj, 8);
    if (na < 1)
      break;

    n->connect_from = node_id;
    n->connect_to = -1;
    for (int i = 0; i < gram->n_nodes; i++)
      if (gram->nodes[i].region_id == adj[0] && i != node_id) {
        n->connect_to = i;
        break;
      }
    break;
  }

  case SRD_ACT_DISCONNECT:
    n->connect_to = -1;
    break;

  case SRD_ACT_MERGE_ROOM:
    n->n_children = 0;
    break;

  case SRD_ACT_CORRIDOR: {
    if (n->connect_to < 0)
      break;
    float ax = n->params[SRD_PARAM_EXTENT_X];
    float az = n->params[SRD_PARAM_EXTENT_Z];
    float bx = 0, bz = 0;
    if (n->connect_to < gram->n_nodes) {
      bx = gram->nodes[n->connect_to].params[SRD_PARAM_EXTENT_X];
      bz = gram->nodes[n->connect_to].params[SRD_PARAM_EXTENT_Z];
    }
    float cx = (ax + bx) * 0.5f;
    float cz = (az + bz) * 0.5f;

    int fid = srd_grammar_add_node(gram, SRD_SYM_FLOOR_SEG, node_id, -1);
    if (fid >= 0) {
      gram->nodes[fid].params[0] = cx;
      gram->nodes[fid].params[3] = cz;
      gram->nodes[fid].params[1] = 1.0f;
      gram->nodes[fid].params[2] = 1.0f;
    }
    int cid = srd_grammar_add_node(gram, SRD_SYM_CEILING_SEG, node_id, -1);
    if (cid >= 0) {
      gram->nodes[cid].params[0] = cx;
      gram->nodes[cid].params[3] = cz;
      gram->nodes[cid].params[1] = 1.0f;
      gram->nodes[cid].params[2] = 1.0f;
    }

    float h = (fabsf(ax - bx) > fabsf(az - bz));
    if (h) {
      int w1 = srd_grammar_add_node(gram, SRD_SYM_WALL_SEG, node_id, -1);
      if (w1 >= 0) {
        gram->nodes[w1].params[0] = cx;
        gram->nodes[w1].params[3] = cz - 2.0f;
        gram->nodes[w1].params[1] = 1.0f;
        gram->nodes[w1].params[2] = 1.0f;
      }
      int w2 = srd_grammar_add_node(gram, SRD_SYM_WALL_SEG, node_id, -1);
      if (w2 >= 0) {
        gram->nodes[w2].params[0] = cx;
        gram->nodes[w2].params[3] = cz + 2.0f;
        gram->nodes[w2].params[1] = 1.0f;
        gram->nodes[w2].params[2] = 1.0f;
      }
    } else {
      int w1 = srd_grammar_add_node(gram, SRD_SYM_WALL_SEG, node_id, -1);
      if (w1 >= 0) {
        gram->nodes[w1].params[0] = cx - 2.0f;
        gram->nodes[w1].params[3] = cz;
        gram->nodes[w1].params[1] = 1.0f;
        gram->nodes[w1].params[2] = 1.0f;
      }
      int w2 = srd_grammar_add_node(gram, SRD_SYM_WALL_SEG, node_id, -1);
      if (w2 >= 0) {
        gram->nodes[w2].params[0] = cx + 2.0f;
        gram->nodes[w2].params[3] = cz;
        gram->nodes[w2].params[1] = 1.0f;
        gram->nodes[w2].params[2] = 1.0f;
      }
    }
    break;
  }

  case SRD_ACT_MERGE_CORRIDOR:
    n->n_children = 0;
    break;

  /* ── EMIT_*: rewrite node symbol to terminal in-place ──────── */
  case SRD_ACT_EMIT_WALL:
    n->symbol = SRD_SYM_WALL;
    break;
  case SRD_ACT_EMIT_VOID:
    n->symbol = SRD_SYM_VOID;
    break;
  case SRD_ACT_EMIT_FLOOR:
    n->symbol = SRD_SYM_FLOOR;
    break;
  case SRD_ACT_EMIT_CEILING:
    n->symbol = SRD_SYM_CEILING;
    break;
  case SRD_ACT_EMIT_STAIR:
    n->symbol = SRD_SYM_STAIR;
    break;

  /* ── TO_DOOR: WALL_SEG → WALL_WITH_DOOR in-place ──────────── */
  case SRD_ACT_TO_DOOR:
    n->symbol = SRD_SYM_WALL_WITH_DOOR;
    break;

  /* ── TO_STAIR: stair room → STAIR_ANCHOR in-place ─────────── */
  case SRD_ACT_TO_STAIR:
    n->symbol = SRD_SYM_STAIR_ANCHOR;
    break;

  default:
    break;
  }

  return 0;
}

/* ── Collect tiles from all terminal nodes ───────────────────── */

int srd_grammar_collect_tiles(const srd_grammar_t *gram, srd_tile_list_t *tiles,
                              const srd_grid_t *grid, float floor_h,
                              float ceil_h) {
  if (!gram || !tiles)
    return -1;
  (void)grid;
  int count = 0;
  int total_checked = 0;
  for (int i = 0; i < gram->n_nodes; i++) {
    const srd_tree_node_t *n = &gram->nodes[i];
    if (n->n_children > 0)
      continue;
    total_checked++;
    if (!srd_symbol_is_terminal(n->symbol))
      continue;

    fprintf(stderr, "COLLECT: node %d sym=%d pos=(%.1f,%.1f) h=(%.1f,%.1f)\n",
            i, n->symbol, n->params[SRD_PARAM_EXTENT_X],
            n->params[SRD_PARAM_EXTENT_Z],
            n->params[SRD_PARAM_WIDTH], n->params[SRD_PARAM_HEIGHT]);

    srd_tile_type_t tt;
    switch (n->symbol) {
    case SRD_SYM_WALL:
      tt = SRD_TILE_WALL;
      break;
    case SRD_SYM_VOID:
      tt = SRD_TILE_VOID;
      break;
    case SRD_SYM_FLOOR:
      tt = SRD_TILE_FLOOR;
      break;
    case SRD_SYM_CEILING:
      tt = SRD_TILE_CEILING;
      break;
    case SRD_SYM_STAIR:
      tt = SRD_TILE_STAIR;
      break;
    default:
      continue;
    }

    srd_tile_list_add(tiles, tt, n->params[SRD_PARAM_EXTENT_X],
                      n->params[SRD_PARAM_EXTENT_Z], floor_h, ceil_h,
                      n->params[SRD_PARAM_WIDTH], n->params[SRD_PARAM_HEIGHT]);
    count++;
  }
  if (count > 0)
    fprintf(stderr, "COLLECT: %d terminal tiles found\n", count);
  return count;
}

/* ── Extract room geometry (compat) ──────────────────────────── */

int srd_grammar_extract(const srd_grammar_t *gram, fr_room_box_t **rooms,
                        uint32_t *n_rooms, fr_corridor_seg_t **corrs,
                        uint32_t *n_corrs) {
  *rooms = NULL;
  *n_rooms = 0;
  *corrs = NULL;
  *n_corrs = 0;
  fr_room_box_t *r =
      (fr_room_box_t *)calloc(gram->n_nodes, sizeof(fr_room_box_t));
  uint32_t nr = 0;
  for (int i = 0; i < gram->n_nodes; i++) {
    const srd_tree_node_t *n = &gram->nodes[i];
    if (n->params[SRD_PARAM_EXTENT_X] > 0.5f) {
      fr_room_box_init(&r[nr]);
      r[nr].center_x = n->params[SRD_PARAM_EXTENT_X];
      r[nr].center_z = n->params[SRD_PARAM_EXTENT_Z];
      r[nr].half_extent_x = 4.0f;
      r[nr].half_extent_z = 4.0f;
      r[nr].floor_z = 0.0f;
      r[nr].ceil_z = 4.0f;
      nr++;
    }
  }
  *rooms = r;
  *n_rooms = nr;
  return 0;
}

/* ── Rule table ──────────────────────────────────────────────── */

static const srd_rule_t RULE_TABLE[] = {

    /* ── Room decomposition (all region symbols) ───────────── */
    /* 0-1. R */
    {0, SRD_SYM_R, 0, {{0}}, SRD_ACT_ROOM, 0, {}, {}},
    {1,
     SRD_SYM_R,
     1,
     {{SRD_COND_HAS_CHILDREN, 0, 0}},
     SRD_ACT_MERGE_ROOM,
     0,
     {},
     {}},
    /* 2-3. B */
    {2, SRD_SYM_B, 0, {{0}}, SRD_ACT_ROOM, 0, {}, {}},
    {3,
     SRD_SYM_B,
     1,
     {{SRD_COND_HAS_CHILDREN, 0, 0}},
     SRD_ACT_MERGE_ROOM,
     0,
     {},
     {}},
    /* 4-5. G */
    {4, SRD_SYM_G, 0, {{0}}, SRD_ACT_ROOM, 0, {}, {}},
    {5,
     SRD_SYM_G,
     1,
     {{SRD_COND_HAS_CHILDREN, 0, 0}},
     SRD_ACT_MERGE_ROOM,
     0,
     {},
     {}},
    /* 6-7. P */
    {6, SRD_SYM_P, 0, {{0}}, SRD_ACT_ROOM, 0, {}, {}},
    {7,
     SRD_SYM_P,
     1,
     {{SRD_COND_HAS_CHILDREN, 0, 0}},
     SRD_ACT_MERGE_ROOM,
     0,
     {},
     {}},

    /* ── CONNECT / DISCONNECT ────────────────────────────────── */
    /* 8-11. CONNECT: link adjacent region nodes */
    {8,
     SRD_SYM_R,
     1,
     {{SRD_COND_ADJACENT_COUNT_GE, 0, 1}},
     SRD_ACT_CONNECT,
     0,
     {},
     {}},
    {9,
     SRD_SYM_B,
     1,
     {{SRD_COND_ADJACENT_COUNT_GE, 0, 1}},
     SRD_ACT_CONNECT,
     0,
     {},
     {}},
    {10,
     SRD_SYM_G,
     1,
     {{SRD_COND_ADJACENT_COUNT_GE, 0, 1}},
     SRD_ACT_CONNECT,
     0,
     {},
     {}},
    {11,
     SRD_SYM_P,
     1,
     {{SRD_COND_ADJACENT_COUNT_GE, 0, 1}},
     SRD_ACT_CONNECT,
     0,
     {},
     {}},
    /* 12-15. DISCONNECT */
    {12,
     SRD_SYM_R,
     1,
     {{SRD_COND_ADJACENT_COUNT_GE, 0, 1}},
     SRD_ACT_DISCONNECT,
     0,
     {},
     {}},
    {13,
     SRD_SYM_G,
     1,
     {{SRD_COND_ADJACENT_COUNT_GE, 0, 1}},
     SRD_ACT_DISCONNECT,
     0,
     {},
     {}},
    {14,
     SRD_SYM_B,
     1,
     {{SRD_COND_ADJACENT_COUNT_GE, 0, 1}},
     SRD_ACT_DISCONNECT,
     0,
     {},
     {}},
    {15,
     SRD_SYM_P,
     1,
     {{SRD_COND_ADJACENT_COUNT_GE, 0, 1}},
     SRD_ACT_DISCONNECT,
     0,
     {},
     {}},

    /* ── CORRIDOR / MERGE_CORRIDOR ───────────────────────────── */
    /* 16-19. CORRIDOR: node with connect_to → create corridor children */
    {16,
     SRD_SYM_R,
     1,
     {{SRD_COND_NO_CHILDREN, 0, 0}},
     SRD_ACT_CORRIDOR,
     0,
     {},
     {}},
    {17,
     SRD_SYM_B,
     1,
     {{SRD_COND_NO_CHILDREN, 0, 0}},
     SRD_ACT_CORRIDOR,
     0,
     {},
     {}},
    {18,
     SRD_SYM_G,
     1,
     {{SRD_COND_NO_CHILDREN, 0, 0}},
     SRD_ACT_CORRIDOR,
     0,
     {},
     {}},
    {19,
     SRD_SYM_P,
     1,
     {{SRD_COND_NO_CHILDREN, 0, 0}},
     SRD_ACT_CORRIDOR,
     0,
     {},
     {}},
    /* 20-23. MERGE_CORRIDOR */
    {20,
     SRD_SYM_R,
     1,
     {{SRD_COND_HAS_CHILDREN, 0, 0}},
     SRD_ACT_MERGE_CORRIDOR,
     0,
     {},
     {}},
    {21,
     SRD_SYM_B,
     1,
     {{SRD_COND_HAS_CHILDREN, 0, 0}},
     SRD_ACT_MERGE_CORRIDOR,
     0,
     {},
     {}},
    {22,
     SRD_SYM_G,
     1,
     {{SRD_COND_HAS_CHILDREN, 0, 0}},
     SRD_ACT_MERGE_CORRIDOR,
     0,
     {},
     {}},
    {23,
     SRD_SYM_P,
     1,
     {{SRD_COND_HAS_CHILDREN, 0, 0}},
     SRD_ACT_MERGE_CORRIDOR,
     0,
     {},
     {}},

    /* ── Stairs ─────────────────────────────────────────────── */
    {24, SRD_SYM_UP, 0, {{0}}, SRD_ACT_TO_STAIR, 0, {}, {}},
    {25, SRD_SYM_DN, 0, {{0}}, SRD_ACT_TO_STAIR, 0, {}, {}},
    /* STAIR_ANCHOR → STAIR terminals (4 steps) */
    {26, SRD_SYM_STAIR_ANCHOR, 0, {{0}}, SRD_ACT_EMIT_STAIR, 0, {}, {}},
    {26, SRD_SYM_STAIR_ANCHOR, 0, {{0}}, SRD_ACT_EMIT_STAIR, 0, {}, {}},
    {26, SRD_SYM_STAIR_ANCHOR, 0, {{0}}, SRD_ACT_EMIT_STAIR, 0, {}, {}},
    {26, SRD_SYM_STAIR_ANCHOR, 0, {{0}}, SRD_ACT_EMIT_STAIR, 0, {}, {}},

    /* ── WALL_SEG → WALL or WALL_WITH_DOOR → VOID ─────────── */
    /* 27. WALL_SEG → WALL terminal (default) */
    {27, SRD_SYM_WALL_SEG, 0, {{0}}, SRD_ACT_EMIT_WALL, 0, {}, {}},
    /* 28. WALL_SEG + door adjacent → WALL_WITH_DOOR */
    {28,
     SRD_SYM_WALL_SEG,
     1,
     {{SRD_COND_WALL_HAS_DOOR, 0, 0}},
     SRD_ACT_TO_DOOR,
     0,
     {},
     {}},
    /* 29. WALL_WITH_DOOR → VOID terminal */
    {29, SRD_SYM_WALL_WITH_DOOR, 0, {{0}}, SRD_ACT_EMIT_VOID, 0, {}, {}},
    /* 30. VOID → back to WALL_WITH_DOOR (inverse) */
    {30, SRD_SYM_VOID, 0, {{0}}, SRD_ACT_TO_DOOR, 0, {}, {}},

    /* ── FLOOR/CEILING segments → terminals ────────────────── */
    {31, SRD_SYM_FLOOR_SEG, 0, {{0}}, SRD_ACT_EMIT_FLOOR, 0, {}, {}},
    {32, SRD_SYM_CEILING_SEG, 0, {{0}}, SRD_ACT_EMIT_CEILING, 0, {}, {}},

};

static const int N_RULES = sizeof(RULE_TABLE) / sizeof(RULE_TABLE[0]);

int srd_rule_count(void) { return N_RULES; }
const srd_rule_t *srd_rule_table(void) { return RULE_TABLE; }

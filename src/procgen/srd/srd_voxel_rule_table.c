/**
 * @file srd_voxel_rule_table.c
 * @brief Default table of all 18 voxel rewrite rules.
 *
 * Non-static functions (1): srd_voxel_rule_table_default
 */
#include "ferrum/procgen/srd/srd_voxel_rule_table.h"
#include "ferrum/procgen/srd/srd_rules_wall.h"
#include "ferrum/procgen/srd/srd_rules_corner.h"
#include "ferrum/procgen/srd/srd_rules_height.h"
#include "ferrum/procgen/srd/srd_rules_vcorridor.h"
#include "ferrum/procgen/srd/srd_rules_vfeature.h"
#include "ferrum/procgen/srd/srd_rules_embellish.h"

/* FACE_WALL is a sentinel meaning "any of N/S/E/W" — the loop picks one
 * at random. We encode this as SRD_FACE_NORTH and let the sampler
 * randomize among the four wall faces. */
#define FACE_WALL  SRD_FACE_NORTH

static const srd_voxel_rule_entry_t DEFAULT_TABLE[] = {
    /* ── Wall rules (face = N/S/E/W, sampler randomises) ─────── */
    { srd_rule_wall_push,   FACE_WALL, 0, 1.0f, 3.0f, "wall_push"   },
    { srd_rule_wall_pull,   FACE_WALL, 0, 1.0f, 3.0f, "wall_pull"   },
    { srd_rule_wall_bevel,  FACE_WALL, 0, 1.0f, 3.0f, "wall_bevel"  },
    { srd_rule_wall_niche,  FACE_WALL, 0, 1.0f, 3.0f, "wall_niche"  },

    /* ── Corner rules (needs_corner = 1) ─────────────────────── */
    { srd_rule_corner_chamfer, SRD_FACE_NONE, 1, 1.0f, 3.0f, "corner_chamfer" },
    { srd_rule_corner_round,   SRD_FACE_NONE, 1, 1.0f, 3.0f, "corner_round"   },

    /* ── Height rules ────────────────────────────────────────── */
    { srd_rule_ceiling_raise, SRD_FACE_CEIL,  0, 1.0f, 3.0f, "ceiling_raise" },
    { srd_rule_ceiling_lower, SRD_FACE_CEIL,  0, 1.0f, 3.0f, "ceiling_lower" },
    { srd_rule_floor_step,    SRD_FACE_FLOOR, 0, 1.0f, 3.0f, "floor_step"    },

    /* ── Corridor rules ──────────────────────────────────────── */
    { srd_rule_corridor_widen,  SRD_FACE_NONE, 0, 1.0f, 2.0f, "corridor_widen"  },
    { srd_rule_corridor_narrow, SRD_FACE_NONE, 0, 1.0f, 2.0f, "corridor_narrow" },

    /* ── Feature rules ───────────────────────────────────────── */
    { srd_rule_add_pillar,    SRD_FACE_NONE, 0, 1.0f, 2.0f, "add_pillar"    },
    { srd_rule_remove_pillar, SRD_FACE_NONE, 0, 1.0f, 2.0f, "remove_pillar" },
    { srd_rule_convert_type,  SRD_FACE_NONE, 0, 1.0f, 3.0f, "convert_type"  },

    /* ── Embellishment rules ─────────────────────────────────── */
    { srd_rule_alcove,         FACE_WALL,      0, 1.0f, 3.0f, "alcove"          },
    { srd_rule_floor_pit,      SRD_FACE_FLOOR, 0, 1.0f, 3.0f, "floor_pit"       },
    { srd_rule_floor_pit_fill, SRD_FACE_FLOOR, 0, 1.0f, 3.0f, "floor_pit_fill"  },
};

#define TABLE_SIZE ((int)(sizeof(DEFAULT_TABLE) / sizeof(DEFAULT_TABLE[0])))

const srd_voxel_rule_entry_t *srd_voxel_rule_table_default(int *n_rules) {
    if (n_rules) *n_rules = TABLE_SIZE;
    return DEFAULT_TABLE;
}

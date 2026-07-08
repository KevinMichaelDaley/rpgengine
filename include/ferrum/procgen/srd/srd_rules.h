#ifndef FERRUM_PROCGEN_SRD_RULES_H
#define FERRUM_PROCGEN_SRD_RULES_H

#include <stdint.h>
#include <stdbool.h>
#include "ferrum/procgen/procgen_srd_types.h"
#include "ferrum/procgen/srd/srd_symbol_grid.h"
#include "ferrum/procgen/srd/srd_grammar_impl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Context conditions ───────────────────────────────────────── */

typedef enum {
    SRD_COND_NONE = 0,
    SRD_COND_ADJACENT_SYMBOL,   /* neighbor matches a specific symbol */
    SRD_COND_ADJACENT_ANY,      /* has any adjacent region */
    SRD_COND_CELL_COUNT_GE,     /* region has at least N cells */
    SRD_COND_ON_BOUNDARY,       /* touches grid boundary */
    SRD_COND_HAS_STAIR,         /* adjacent to stair anchor */
} srd_cond_type_t;

typedef struct {
    srd_cond_type_t type;
    char  symbol;         /* for ADJACENT_SYMBOL */
    int   int_value;      /* for CELL_COUNT_GE */
} srd_cond_t;

/* ── Expansion result ─────────────────────────────────────────── */

typedef enum {
    SRD_EXPAND_ROOM = 0,
    SRD_EXPAND_ROOM_DOOR,
    SRD_EXPAND_ROOM_WALL,
    SRD_EXPAND_CORRIDOR,
    SRD_EXPAND_STAIR,
    SRD_EXPAND_SPLIT,
    SRD_EXPAND_MERGE,
} srd_expand_type_t;

typedef struct {
    srd_expand_type_t type;
    float param[6];       /* continuous params: pos, size, etc. */
    int   target_node;    /* for corridors: other room node */
    int   direction;      /* 0=N,1=S,2=E,3=W */
} srd_expand_t;

/* ── Grammar rule ─────────────────────────────────────────────── */

typedef struct {
    char           symbol;            /* 'R', 'B', 'G', 'P', '^', 'v', 'W', '.' */
    int            n_conditions;
    srd_cond_t     conditions[4];
    srd_expand_t   expansion;
    int            n_params;          /* number of learnable continuous params */
    int            param_indices[6];  /* which expansion.param[] entries are tuned */
} srd_rule_t;

/* ── Rule evaluation ──────────────────────────────────────────── */

/**
 * @brief Check if a rule's conditions are satisfied for a given region.
 */
bool srd_rule_matches(const srd_rule_t *rule,
                       const srd_symbol_grid_t *grid,
                       int region_id);

/**
 * @brief Apply a rule's expansion to a grammar tree node.
 * Creates child nodes for the expansion result.
 */
int  srd_rule_apply(srd_grammar_tree_t *tree, int node_id,
                     const srd_rule_t *rule,
                     const srd_symbol_grid_t *grid);

/**
 * @brief Find all matching rules for a region.
 * Returns number of matching rules (writes indices into out_rules).
 */
int  srd_rule_find_matches(const srd_symbol_grid_t *grid,
                            int region_id,
                            const srd_rule_t *rules, int n_rules,
                            int *out_rule_indices, int max_out);

/* ── All rules table ──────────────────────────────────────────── */

/** Returns number of rules in the table. */
int  srd_rule_count(void);

/** Returns pointer to the rule table. */
const srd_rule_t *srd_rule_table(void);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROCGEN_SRD_RULES_H */

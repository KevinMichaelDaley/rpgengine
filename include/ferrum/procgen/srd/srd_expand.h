#ifndef FERRUM_PROCGEN_SRD_EXPAND_H
#define FERRUM_PROCGEN_SRD_EXPAND_H

#include <stdint.h>
#include "ferrum/procgen/srd/srd_symbol_grid.h"
#include "ferrum/procgen/srd/srd_grammar_impl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Expand a region node into one or more children based on
 *        its symbol type and neighbor context.  Returns the number
 *        of expansions proposed (written to the tree).
 *
 * Called repeatedly until no more non-terminals remain.
 */
int srd_expand_region(srd_grammar_tree_t *tree, int node_id,
                       const srd_symbol_grid_t *grid,
                       int which_rule);

/**
 * @brief Expand all non-terminals in the tree using one rule each.
 * Returns number of expansions performed.
 */
int srd_expand_all(srd_grammar_tree_t *tree,
                    const srd_symbol_grid_t *grid,
                    const int *rule_selections,
                    int n_rules);

/**
 * @brief Count how many possible expansion rules exist for a given node.
 */
int srd_count_expansion_rules(const srd_tree_node_t *node,
                               const srd_symbol_grid_t *grid);

/**
 * @brief Evaluate the energy of the current tree (terminal nodes only)
 *        using SymX via the energy bridge.
 */
double srd_tree_evaluate_energy(srd_grammar_tree_t *tree,
                                 const double *room_params,
                                 int n_params);

/**
 * @brief Extract terminal geometry from the tree.
 * Returns room and corridor counts.
 */
int srd_tree_extract_geometry(const srd_grammar_tree_t *tree,
                               fr_room_box_t **rooms_out, uint32_t *n_rooms,
                               fr_corridor_seg_t **corrs_out, uint32_t *n_corrs);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROCGEN_SRD_EXPAND_H */

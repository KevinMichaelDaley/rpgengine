/**
 * @file srd_discrete_maxcover.h
 * @brief Compatibility graph and greedy max-cover for SRD candidates.
 *
 * Two candidates are compatible iff the sets of boxes within
 * locality_radius of their respective selections do not overlap.
 * Greedy max-cover selects the highest-delta_L non-conflicting subset.
 *
 * Non-static functions declared (2): srd_build_compatibility,
 *                                     srd_greedy_max_cover
 */
#ifndef FERRUM_PROCGEN_SRD_DISCRETE_MAXCOVER_H
#define FERRUM_PROCGEN_SRD_DISCRETE_MAXCOVER_H

#include "ferrum/procgen/srd/srd_discrete_candidates.h"
#include "ferrum/procgen/srd/srd_descent_rules.h"
#include "ferrum/procgen/srd/srd_sdf_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build an NxN compatibility matrix for candidates.
 *
 * Two candidates are compatible (compat[i*stride+j]=1) iff no box
 * falls within locality_radius of both candidates' selections.
 * The diagonal is always 0 (a candidate is not compatible with itself).
 *
 * @param tbl        Rule table (for locality_radius per rule).
 * @param cands      Array of candidates.
 * @param n          Number of candidates.
 * @param layout     Reference layout (for box positions).
 * @param compat     Output: flat NxN uint8 matrix, row-major.
 * @param stride     Row stride of compat (>= n).
 *
 * @note Ownership: compat is caller-owned. Must have size >= n*stride.
 */
void srd_build_compatibility(const srd_rule_table_t *tbl,
                             const srd_candidate_t *cands,
                             int n,
                             const srd_sdf_layout_t *layout,
                             uint8_t *compat,
                             int stride);

/**
 * @brief Greedy max-cover: select non-conflicting positive-delta_L candidates.
 *
 * Sorts candidates by delta_L descending. Greedily includes each that
 * is compatible with all already-selected candidates. Only considers
 * candidates with delta_L > 0.
 *
 * @param cands      Array of candidates.
 * @param n          Number of candidates.
 * @param compat     Compatibility matrix from srd_build_compatibility.
 * @param stride     Row stride of compat.
 * @param out        Output: indices of selected candidates.
 * @param max_out    Capacity of out.
 * @return Number of selected candidates.
 *
 * @note Ownership: out is caller-owned.
 */
int srd_greedy_max_cover(const srd_candidate_t *cands,
                         int n,
                         const uint8_t *compat,
                         int stride,
                         int *out,
                         int max_out);

#ifdef __cplusplus
}
#endif
#endif /* FERRUM_PROCGEN_SRD_DISCRETE_MAXCOVER_H */

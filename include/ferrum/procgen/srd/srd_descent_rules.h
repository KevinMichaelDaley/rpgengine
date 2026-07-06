/**
 * @file srd_descent_rules.h
 * @brief Rule table API for SRD descent-friendly grammar rules.
 *
 * Rules are function-pointer-based: each rule has a condition predicate
 * and an apply function. Rules are registered into a table, which can
 * hold both built-in dungeon rules and custom user-defined rules.
 *
 * Design follows Kodnongbua et al., "Design for Descent" (SIGGRAPH Asia 2025):
 * - REVERSIBILITY: every non-repair rule has a registered inverse
 * - JUMP CONTINUITY: Add* rules spawn at SRD_EPSILON
 * - LOCAL GEOMETRIC CONTROL: BridgeComponents, AddDeadEnd, etc.
 * - REPAIRABILITY: repair rules project to feasibility
 */
#ifndef FERRUM_PROCGEN_SRD_DESCENT_RULES_H
#define FERRUM_PROCGEN_SRD_DESCENT_RULES_H

#include "ferrum/procgen/srd/srd_sdf_layout.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Selection ──────────────────────────────────────────────────── */

/** @brief Maximum number of box indices in a selection. */
#define SRD_MAX_SELECT 8

/**
 * @brief Identifies which boxes a rule operates on.
 *
 * Filled by srd_rule_sample_selection or by the caller directly.
 */
typedef struct {
    int indices[SRD_MAX_SELECT]; /**< Box indices in the layout */
    int n;                       /**< Number of valid entries */
} srd_selection_t;

/* ── Rule function signatures ───────────────────────────────────── */

/**
 * @brief Rule condition predicate.
 *
 * Returns true if the rule can legally fire on the given selection.
 * Must not modify the layout.
 *
 * @param layout   Current layout state (read-only).
 * @param sel      Selected boxes to operate on.
 * @param userdata Caller-supplied context (may be NULL).
 * @return true if the rule can fire.
 */
typedef bool (*srd_rule_cond_fn)(const srd_sdf_layout_t *layout,
                                 const srd_selection_t  *sel,
                                 const void             *userdata);

/**
 * @brief Rule apply function.
 *
 * Modifies layout in-place. On success, returns the number of new boxes
 * added (0 if no boxes were created, e.g. ConvertType). On failure,
 * returns negative and the layout must be unchanged.
 *
 * @param layout          Layout to modify.
 * @param sel             Selected boxes to operate on.
 * @param userdata        Caller-supplied context (may be NULL).
 * @param new_box_indices Out-array: indices of any boxes added.
 * @param cap_new_boxes   Capacity of new_box_indices.
 * @return Number of new boxes on success, or negative on failure.
 *
 * @note Ownership: layout is modified in-place; caller owns it.
 * @note Side effects: may add or remove boxes; may change adjacency.
 */
typedef int (*srd_rule_apply_fn)(srd_sdf_layout_t      *layout,
                                 const srd_selection_t *sel,
                                 const void            *userdata,
                                 int                   *new_box_indices,
                                 int                    cap_new_boxes);

/* ── Rule descriptor ────────────────────────────────────────────── */

/**
 * @brief Descriptor for a single rewrite rule.
 *
 * Registered into the rule table via srd_rule_table_register.
 * Built-in rules are registered by srd_rule_table_register_builtins.
 *
 * @note inverse_rule_id: for non-repair rules, this must point to a
 *       valid rule in the table. Debug builds assert this on registration.
 *       Repair rules set this to -1.
 */
typedef struct {
    const char       *name;            /**< Human-readable rule name */
    int               inverse_rule_id; /**< Index of inverse rule, or -1 */
    int               n_select;        /**< How many boxes the rule picks */
    float             locality_radius; /**< World units: affected neighbourhood */
    bool              is_repair;       /**< If true, not in candidate set */
    bool              jump_continuous; /**< Assertion: rendered change < SRD_EPSILON */
    srd_rule_cond_fn  cond;            /**< Condition predicate */
    srd_rule_apply_fn apply;           /**< Apply function */
    void             *userdata;        /**< Passed through to cond/apply */
} srd_descent_rule_t;

/* ── Rule table ─────────────────────────────────────────────────── */

/** @brief Maximum rules in a single table. */
#define SRD_MAX_RULES_TABLE 512

/**
 * @brief Rule table holding registered rules.
 *
 * Stack-allocatable. No dynamic memory.
 */
typedef struct {
    srd_descent_rule_t rules[SRD_MAX_RULES_TABLE]; /**< Registered rules */
    int                n_rules;                     /**< Number registered */
} srd_rule_table_t;

/**
 * @brief Initialise an empty rule table.
 *
 * @param[out] tbl  Table to initialise. Must not be NULL.
 */
void srd_rule_table_init(srd_rule_table_t *tbl);

/**
 * @brief Register a rule in the table.
 *
 * @param tbl   Table to add to.
 * @param rule  Rule descriptor (copied by value).
 * @return Index of the registered rule, or -1 if table is full.
 *
 * @note In debug builds: if rule->inverse_rule_id >= 0, asserts that
 *       the referenced rule is already registered.
 */
int srd_rule_table_register(srd_rule_table_t *tbl,
                            const srd_descent_rule_t *rule);

/**
 * @brief Register all built-in dungeon layout rules (46 rules).
 *
 * @param tbl  Table to populate. Should be freshly initialised.
 */
void srd_rule_table_register_builtins(srd_rule_table_t *tbl);

/**
 * @brief Find all non-repair rules applicable to a layout.
 *
 * Scans all registered rules. For each non-repair rule, checks if
 * there exists at least one valid selection by trying up to 32 random
 * selections.
 *
 * @param tbl              Rule table.
 * @param layout           Current layout.
 * @param out_rule_indices Out-array of applicable rule indices.
 * @param max_out          Capacity of out_rule_indices.
 * @param rng_state        Pointer to xorshift32 RNG state.
 * @return Number of applicable rules found.
 */
int srd_rule_find_applicable(const srd_rule_table_t *tbl,
                             const srd_sdf_layout_t *layout,
                             int *out_rule_indices, int max_out,
                             uint32_t *rng_state);

/**
 * @brief Sample a valid selection for a rule.
 *
 * Tries up to 32 random selections and returns the first one that
 * passes the rule's condition predicate.
 *
 * @param tbl       Rule table.
 * @param rule_idx  Index of the rule.
 * @param layout    Current layout.
 * @param sel_out   Output: the sampled selection.
 * @param rng_state Pointer to xorshift32 RNG state (modified).
 * @return true if a valid selection was found, false otherwise.
 */
bool srd_rule_sample_selection(const srd_rule_table_t *tbl,
                               int rule_idx,
                               const srd_sdf_layout_t *layout,
                               srd_selection_t *sel_out,
                               uint32_t *rng_state);

#ifdef __cplusplus
}
#endif
#endif /* FERRUM_PROCGEN_SRD_DESCENT_RULES_H */

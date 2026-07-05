#ifndef FERRUM_PROCGEN_SRD_GRAMMAR_H
#define FERRUM_PROCGEN_SRD_GRAMMAR_H

#include <stdint.h>
#include "ferrum/procgen/procgen_srd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FR_REWRITE_SPLIT_ROOM = 0,
    FR_REWRITE_MERGE_ROOMS = 1,
    FR_REWRITE_RESIZE_ROOM = 2,
    FR_REWRITE_ADD_ROOM = 3,
    FR_REWRITE_REMOVE_ROOM = 4,
    FR_REWRITE_ADD_CONNECTION = 5,
    FR_REWRITE_REMOVE_CONNECTION = 6,
    FR_REWRITE_INSERT_OPENING = 7,
    FR_REWRITE_EXPAND_STAIR = 8,
    FR_REWRITE_WIDEN_CONNECTION = 9,
} fr_rewrite_type_t;

typedef enum {
    FR_ROOM_BOX_TYPE = 0,
    FR_CORRIDOR_TYPE = 1,
    FR_STAIR_TYPE = 2,
} fr_element_type_t;

typedef struct {
    fr_rewrite_type_t type;
    uint32_t          element_indices[4];
    float             param_float[4];
    int32_t           param_int[2];
} fr_rewrite_proposal_t;

/**
 * @brief Propose rewrites for a single element.
 *
 * Context check: only proposals that make sense for this element
 * (given its current parameters) are emitted.
 *
 * @param element     The room/corridor/stair element.
 * @param other_elements  Array of other elements (for adjacency checks).
 * @param n_other     Number of other elements.
 * @param element_type Type of the element.
 * @param element_id  Index of the element in the global list.
 * @param split_threshold  Half-extent beyond which split is proposed.
 * @param proposals_out  Output array of proposals.
 * @param cap          Capacity of proposals_out.
 * @param count_out    Number of proposals written.
 * @return 0 on success, -1 on error.
 */
int procgen_srd_propose_rewrites(const void *element,
                                 const void *other_elements,
                                 uint32_t n_other,
                                 fr_element_type_t element_type,
                                 uint32_t element_id,
                                 float split_threshold,
                                 fr_rewrite_proposal_t *proposals_out,
                                 uint32_t cap, uint32_t *count_out);

/**
 * @brief Propose rewrites for multiple elements at once.
 *
 * Also considers pairwise proposals like merge and connect.
 */
int procgen_srd_propose_rewrites_multiple(const fr_room_box_t *rooms,
                                          uint32_t n_rooms,
                                          float min_half_extent,
                                          float split_threshold,
                                          fr_rewrite_proposal_t *proposals_out,
                                          uint32_t cap, uint32_t *count_out);

/**
 * @brief Apply a rewrite proposal to an array of elements.
 *
 * Modifies the elements array and its count.  Caller owns the
 * elements array and individual element allocations.
 *
 * @param elements   Array of void* pointing to elements (RoomBox, etc.).
 * @param count_inout Pointer to current count; updated on success.
 * @param cap         Capacity of the elements array.
 * @param proposal   The rewrite to apply.
 * @return 0 on success, -1 on error.
 */
int procgen_srd_apply_rewrite(void **elements, uint32_t *count_inout,
                              uint32_t cap,
                              const fr_rewrite_proposal_t *proposal);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROCGEN_SRD_GRAMMAR_H */

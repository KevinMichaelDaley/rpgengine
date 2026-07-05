#include "ferrum/procgen/procgen_srd_grammar.h"

#include <math.h>
#include <string.h>

int procgen_srd_propose_rewrites(const void *element,
                                 const void *other_elements,
                                 uint32_t n_other,
                                 fr_element_type_t element_type,
                                 uint32_t element_id,
                                 float split_threshold,
                                 fr_rewrite_proposal_t *proposals_out,
                                 uint32_t cap, uint32_t *count_out) {
    if (!element || !proposals_out || !count_out || cap == 0) return -1;
    (void)other_elements;
    (void)n_other;

    uint32_t n = 0;
    const fr_room_box_t *room = (const fr_room_box_t *)element;

    if (element_type == FR_ROOM_BOX_TYPE) {
        /* ── Split room (horizontal) ── */
        if (room->half_extent_x > split_threshold) {
            if (n < cap) {
                proposals_out[n].type = FR_REWRITE_SPLIT_ROOM;
                proposals_out[n].element_indices[0] = element_id;
                proposals_out[n].param_float[0] = 0.0f; /* axis: 0=X, 1=Z */
                proposals_out[n].param_float[1] = 0.5f; /* fraction */
                proposals_out[n].param_int[0] = 0;
                n++;
            }
        }
        /* ── Split room (vertical) ── */
        if (room->half_extent_z > split_threshold) {
            if (n < cap) {
                proposals_out[n].type = FR_REWRITE_SPLIT_ROOM;
                proposals_out[n].element_indices[0] = element_id;
                proposals_out[n].param_float[0] = 1.0f; /* axis Z */
                proposals_out[n].param_float[1] = 0.5f;
                proposals_out[n].param_int[0] = 1;
                n++;
            }
        }
        /* ── Split with 1/3 fraction ── */
        if (room->half_extent_x > split_threshold) {
            if (n < cap) {
                proposals_out[n].type = FR_REWRITE_SPLIT_ROOM;
                proposals_out[n].element_indices[0] = element_id;
                proposals_out[n].param_float[0] = 0.0f;
                proposals_out[n].param_float[1] = 0.333f;
                proposals_out[n].param_int[0] = 0;
                n++;
            }
        }
        if (room->half_extent_z > split_threshold) {
            if (n < cap) {
                proposals_out[n].type = FR_REWRITE_SPLIT_ROOM;
                proposals_out[n].element_indices[0] = element_id;
                proposals_out[n].param_float[0] = 1.0f;
                proposals_out[n].param_float[1] = 0.333f;
                proposals_out[n].param_int[0] = 1;
                n++;
            }
        }

        /* ── Resize room (shrink or grow) ── */
        if (n < cap) {
            proposals_out[n].type = FR_REWRITE_RESIZE_ROOM;
            proposals_out[n].element_indices[0] = element_id;
            proposals_out[n].param_float[0] = 0.0f;  /* X axis */
            proposals_out[n].param_float[1] = 1.0f;  /* +1 cell */
            n++;
        }
        if (n < cap) {
            proposals_out[n].type = FR_REWRITE_RESIZE_ROOM;
            proposals_out[n].element_indices[0] = element_id;
            proposals_out[n].param_float[0] = 1.0f;  /* Z axis */
            proposals_out[n].param_float[1] = -1.0f; /* -1 cell */
            n++;
        }
    }

    /* ── Stair expansion ── */
    if (element_type == FR_STAIR_TYPE) {
        if (n < cap) {
            proposals_out[n].type = FR_REWRITE_EXPAND_STAIR;
            proposals_out[n].element_indices[0] = element_id;
            proposals_out[n].param_float[0] = 0.25f; /* step_h */
            proposals_out[n].param_float[1] = 0.5f;  /* step_d */
            proposals_out[n].param_int[0] = 0;       /* direction N */
            n++;
        }
        if (n < cap) {
            proposals_out[n].type = FR_REWRITE_EXPAND_STAIR;
            proposals_out[n].element_indices[0] = element_id;
            proposals_out[n].param_float[0] = 0.25f;
            proposals_out[n].param_float[1] = 0.5f;
            proposals_out[n].param_int[0] = 1;       /* direction E */
            n++;
        }
    }

    *count_out = n;
    return 0;
}

int procgen_srd_propose_rewrites_multiple(const fr_room_box_t *rooms,
                                          uint32_t n_rooms,
                                          float min_half_extent,
                                          float split_threshold,
                                          fr_rewrite_proposal_t *proposals_out,
                                          uint32_t cap, uint32_t *count_out) {
    if (!rooms || !proposals_out || !count_out || cap == 0) return -1;

    uint32_t n = 0;

    /* ── Per-room proposals ── */
    for (uint32_t i = 0; i < n_rooms; i++) {
        uint32_t sub_count = 0;
        procgen_srd_propose_rewrites(&rooms[i], NULL, 0, FR_ROOM_BOX_TYPE, i,
                                     split_threshold,
                                     proposals_out + n, cap - n, &sub_count);
        n += sub_count;
    }

    /* ── Pairwise proposals ── */
    for (uint32_t i = 0; i < n_rooms; i++) {
        for (uint32_t j = i + 1; j < n_rooms; j++) {
            /* Merge: both rooms are small */
            if (rooms[i].half_extent_x < min_half_extent
                && rooms[i].half_extent_z < min_half_extent
                && rooms[j].half_extent_x < min_half_extent
                && rooms[j].half_extent_z < min_half_extent) {
                if (n < cap) {
                    proposals_out[n].type = FR_REWRITE_MERGE_ROOMS;
                    proposals_out[n].element_indices[0] = i;
                    proposals_out[n].element_indices[1] = j;
                    n++;
                }
            }

            /* Connect: adjacent rooms */
            {
                float dx = rooms[i].center_x - rooms[j].center_x;
                float dz = rooms[i].center_z - rooms[j].center_z;
                float d = sqrtf(dx*dx + dz*dz);
                float sum_hx = rooms[i].half_extent_x + rooms[j].half_extent_x;
                float sum_hz = rooms[i].half_extent_z + rooms[j].half_extent_z;
                float touch_dist = sum_hx + sum_hz + 4.0f; /* generous threshold */
                if (d < touch_dist && n < cap) {
                    proposals_out[n].type = FR_REWRITE_ADD_CONNECTION;
                    proposals_out[n].element_indices[0] = i;
                    proposals_out[n].element_indices[1] = j;
                    proposals_out[n].param_float[0] = 2.0f; /* default width 2m */
                    n++;
                }
            }
        }
    }

    *count_out = n;
    return 0;
}

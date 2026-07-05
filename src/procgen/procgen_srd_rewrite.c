#include "ferrum/procgen/procgen_srd_grammar.h"

#include <stdlib.h>
#include <string.h>

int procgen_srd_apply_rewrite(void **elements, uint32_t *count_inout,
                              uint32_t cap,
                              const fr_rewrite_proposal_t *proposal) {
    if (!elements || !count_inout || !proposal) return -1;
    uint32_t n = *count_inout;

    switch (proposal->type) {
    case FR_REWRITE_SPLIT_ROOM: {
        uint32_t idx = proposal->element_indices[0];
        if (idx >= n || n >= cap) return -1;

        fr_room_box_t *old = (fr_room_box_t *)elements[idx];
        float axis = proposal->param_float[0];
        float frac = proposal->param_float[1];

        fr_room_box_t *a = (fr_room_box_t *)calloc(1, sizeof(fr_room_box_t));
        fr_room_box_t *b = (fr_room_box_t *)calloc(1, sizeof(fr_room_box_t));
        if (!a || !b) { free(a); free(b); return -1; }

        memcpy(a, old, sizeof(fr_room_box_t));
        memcpy(b, old, sizeof(fr_room_box_t));

        if (axis < 0.5f) {
            a->half_extent_x = old->half_extent_x * frac;
            b->half_extent_x = old->half_extent_x * (1.0f - frac);
            a->center_x = old->center_x - old->half_extent_x + a->half_extent_x;
            b->center_x = old->center_x + old->half_extent_x - b->half_extent_x;
        } else {
            a->half_extent_z = old->half_extent_z * frac;
            b->half_extent_z = old->half_extent_z * (1.0f - frac);
            a->center_z = old->center_z - old->half_extent_z + a->half_extent_z;
            b->center_z = old->center_z + old->half_extent_z - b->half_extent_z;
        }

        elements[idx] = a;       /* replace old slot with new a */
        for (uint32_t i = n; i > idx + 1; i--)
            elements[i] = elements[i - 1];
        elements[idx + 1] = b;   /* insert b next to a */
        (*count_inout)++;
        break;
    }
    case FR_REWRITE_MERGE_ROOMS: {
        uint32_t ia = proposal->element_indices[0];
        uint32_t ib = proposal->element_indices[1];
        if (ia >= n || ib >= n || ia >= ib) return -1;

        fr_room_box_t *ra = (fr_room_box_t *)elements[ia];
        fr_room_box_t *rb = (fr_room_box_t *)elements[ib];

        float xmin = ra->center_x - ra->half_extent_x;
        float xmax = ra->center_x + ra->half_extent_x;
        float zmin = ra->center_z - ra->half_extent_z;
        float zmax = ra->center_z + ra->half_extent_z;
        if (rb->center_x - rb->half_extent_x < xmin) xmin = rb->center_x - rb->half_extent_x;
        if (rb->center_x + rb->half_extent_x > xmax) xmax = rb->center_x + rb->half_extent_x;
        if (rb->center_z - rb->half_extent_z < zmin) zmin = rb->center_z - rb->half_extent_z;
        if (rb->center_z + rb->half_extent_z > zmax) zmax = rb->center_z + rb->half_extent_z;

        ra->center_x      = (xmin + xmax) * 0.5f;
        ra->center_z      = (zmin + zmax) * 0.5f;
        ra->half_extent_x = (xmax - xmin) * 0.5f;
        ra->half_extent_z = (zmax - zmin) * 0.5f;

        /* Shift elements after ib down by 1 */
        for (uint32_t i = ib; i < n - 1; i++)
            elements[i] = elements[i + 1];
        (*count_inout)--;
        break;
    }
    case FR_REWRITE_REMOVE_ROOM: {
        uint32_t idx = proposal->element_indices[0];
        if (idx >= n) return -1;
        for (uint32_t i = idx; i < n - 1; i++)
            elements[i] = elements[i + 1];
        (*count_inout)--;
        break;
    }
    default:
        break;
    }

    return 0;
}

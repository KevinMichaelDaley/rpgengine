/**
 * @file mesh_sel_bitset.c
 * @brief Dynamic bitset for mesh element selection.
 *
 * 4 non-static functions: init, destroy, set, unset.
 * toggle, test, clear_all are in mesh_sel_bitset_ops.c.
 */
#include "ferrum/editor/mesh/mesh_edit.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Internal helpers                                                          */
/* ------------------------------------------------------------------------ */

/** @brief Ensure the bitset can hold bit `index`. Grows by doubling. */
static bool ensure_capacity_(mesh_sel_bitset_t *bs, uint32_t index) {
    uint32_t word = index / 64;
    if (word < bs->capacity) { return true; }

    uint32_t new_cap = bs->capacity ? bs->capacity : 4;
    while (new_cap <= word) { new_cap *= 2; }

    uint64_t *buf = realloc(bs->bits, (size_t)new_cap * sizeof(uint64_t));
    if (!buf) { return false; }

    /* Zero-fill new words */
    memset(buf + bs->capacity, 0,
           (size_t)(new_cap - bs->capacity) * sizeof(uint64_t));
    bs->bits     = buf;
    bs->capacity = new_cap;
    return true;
}

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

void mesh_sel_bitset_init(mesh_sel_bitset_t *bs) {
    if (!bs) { return; }
    bs->bits     = NULL;
    bs->capacity = 0;
    bs->count    = 0;
}

void mesh_sel_bitset_destroy(mesh_sel_bitset_t *bs) {
    if (!bs) { return; }
    free(bs->bits);
    bs->bits     = NULL;
    bs->capacity = 0;
    bs->count    = 0;
}

void mesh_sel_bitset_set(mesh_sel_bitset_t *bs, uint32_t index) {
    if (!bs) { return; }
    if (!ensure_capacity_(bs, index)) { return; }

    uint32_t word = index / 64;
    uint64_t bit  = (uint64_t)1 << (index % 64);

    if (!(bs->bits[word] & bit)) {
        bs->bits[word] |= bit;
        bs->count++;
    }
}

void mesh_sel_bitset_unset(mesh_sel_bitset_t *bs, uint32_t index) {
    if (!bs) { return; }

    uint32_t word = index / 64;
    if (word >= bs->capacity) { return; }

    uint64_t bit = (uint64_t)1 << (index % 64);
    if (bs->bits[word] & bit) {
        bs->bits[word] &= ~bit;
        bs->count--;
    }
}

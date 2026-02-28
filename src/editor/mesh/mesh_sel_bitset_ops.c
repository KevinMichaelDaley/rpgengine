/**
 * @file mesh_sel_bitset_ops.c
 * @brief Bitset query and bulk operations — toggle, test, clear_all.
 */
#include "ferrum/editor/mesh/mesh_edit.h"

#include <string.h>

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

void mesh_sel_bitset_toggle(mesh_sel_bitset_t *bs, uint32_t index) {
    if (!bs) { return; }
    if (mesh_sel_bitset_test(bs, index)) {
        mesh_sel_bitset_unset(bs, index);
    } else {
        mesh_sel_bitset_set(bs, index);
    }
}

bool mesh_sel_bitset_test(const mesh_sel_bitset_t *bs, uint32_t index) {
    if (!bs) { return false; }
    uint32_t word = index / 64;
    if (word >= bs->capacity) { return false; }
    uint64_t bit = (uint64_t)1 << (index % 64);
    return (bs->bits[word] & bit) != 0;
}

void mesh_sel_bitset_clear_all(mesh_sel_bitset_t *bs) {
    if (!bs) { return; }
    if (bs->bits && bs->capacity > 0) {
        memset(bs->bits, 0, (size_t)bs->capacity * sizeof(uint64_t));
    }
    bs->count = 0;
}

/**
 * @file entity_attrs_search.c
 * @brief Binary search for the entity attribute directory.
 */

#include "ferrum/entity/entity_attrs.h"

bool entity_attrs_search(const attr_entry_t *entries, uint16_t count,
                         uint16_t key, uint16_t *out_idx)
{
    uint16_t lo = 0;
    uint16_t hi = count;

    while (lo < hi) {
        uint16_t mid = lo + (uint16_t)((hi - lo) / 2);
        if (entries[mid].key < key) {
            lo = (uint16_t)(mid + 1);
        } else if (entries[mid].key > key) {
            hi = mid;
        } else {
            *out_idx = mid;
            return true; /* found */
        }
    }

    *out_idx = lo; /* insertion point */
    return false;
}

/**
 * @file entity_attrs.c
 * @brief Dynamic key-value attribute storage: init, set, get, remove.
 *
 * The attribute block uses a sorted directory of attr_entry_t entries
 * (binary search for lookup) and a packed payload region that grows
 * from the start. When an attribute is removed or resized, the payload
 * is compacted and all offsets are rewritten.
 */

#include "ferrum/entity/entity_attrs.h"

#include <string.h>

/* ----------------------------------------------------------------------- */
/* init / clear / count                                                      */
/* ----------------------------------------------------------------------- */

void entity_attrs_init(entity_attrs_t *attrs)
{
    attrs->count = 0;
    attrs->used  = 0;
}

void entity_attrs_clear(entity_attrs_t *attrs)
{
    attrs->count = 0;
    attrs->used  = 0;
}

uint16_t entity_attrs_count(const entity_attrs_t *attrs)
{
    return attrs->count;
}

/* ----------------------------------------------------------------------- */
/* get                                                                       */
/* ----------------------------------------------------------------------- */

const void *entity_attrs_get(const entity_attrs_t *attrs, uint16_t key,
                             uint8_t *out_type, uint8_t *out_size)
{
    uint16_t idx;
    if (!entity_attrs_search(attrs->entries, attrs->count, key, &idx)) {
        return NULL;
    }

    const attr_entry_t *e = &attrs->entries[idx];
    if (out_type) *out_type = e->type;
    if (out_size) *out_size = e->size;
    return &attrs->payload[e->offset];
}

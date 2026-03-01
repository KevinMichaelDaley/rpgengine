/**
 * @file entity_attrs_mutate.c
 * @brief entity_attrs_t set and remove operations.
 *
 * Set inserts or updates an attribute. Remove deletes and compacts.
 * Both maintain the sorted directory invariant and compact the payload
 * region as needed.
 */

#include "ferrum/entity/entity_attrs.h"

#include <string.h>

/* Maximum payload capacity (derived from struct layout). */
#define PAYLOAD_CAP ((uint16_t)sizeof(((entity_attrs_t *)0)->payload))

/* ----------------------------------------------------------------------- */
/* Internal: compact payload after removing bytes at a given offset.         */
/* ----------------------------------------------------------------------- */

/**
 * @brief Remove `len` bytes at `offset` from payload and adjust all
 *        directory entries whose offsets are above the removed region.
 */
static void compact_payload(entity_attrs_t *attrs, uint16_t offset,
                            uint8_t len)
{
    /* Shift payload bytes down. */
    uint16_t tail = attrs->used - offset - len;
    if (tail > 0) {
        memmove(&attrs->payload[offset],
                &attrs->payload[offset + len],
                tail);
    }
    attrs->used -= len;

    /* Adjust offsets in directory entries that pointed past the gap. */
    for (uint16_t i = 0; i < attrs->count; i++) {
        if (attrs->entries[i].offset > offset) {
            attrs->entries[i].offset -= len;
        }
    }
}

/* ----------------------------------------------------------------------- */
/* set                                                                       */
/* ----------------------------------------------------------------------- */

bool entity_attrs_set(entity_attrs_t *attrs, uint16_t key,
                      uint8_t type, const void *data, uint8_t size)
{
    uint16_t idx;
    bool found = entity_attrs_search(attrs->entries, attrs->count, key, &idx);

    if (found) {
        /* Update existing entry. */
        attr_entry_t *e = &attrs->entries[idx];

        if (e->size == size) {
            /* Same size — overwrite in place. */
            e->type = type;
            memcpy(&attrs->payload[e->offset], data, size);
            return true;
        }

        /* Different size — remove old payload, append new. */
        uint16_t old_offset = e->offset;
        uint8_t  old_size   = e->size;
        compact_payload(attrs, old_offset, old_size);

        /* Check capacity for new payload. */
        if ((uint16_t)(attrs->used + size) > PAYLOAD_CAP) {
            /* Not enough space — restore is complex, so just fail.
             * The old value has been removed at this point. Remove the
             * directory entry to keep consistency. */
            uint16_t tail_entries = attrs->count - idx - 1;
            if (tail_entries > 0) {
                memmove(&attrs->entries[idx],
                        &attrs->entries[idx + 1],
                        tail_entries * sizeof(attr_entry_t));
            }
            attrs->count--;
            return false;
        }

        /* Append new payload. */
        e->type   = type;
        e->size   = size;
        e->offset = attrs->used;
        memcpy(&attrs->payload[attrs->used], data, size);
        attrs->used += size;
        return true;
    }

    /* Insert new entry. */
    if (attrs->count >= ENTITY_ATTRS_MAX_ENTRIES) {
        return false; /* directory full */
    }
    if ((uint16_t)(attrs->used + size) > PAYLOAD_CAP) {
        return false; /* payload full */
    }

    /* Shift directory entries to make room at idx. */
    uint16_t tail_entries = attrs->count - idx;
    if (tail_entries > 0) {
        memmove(&attrs->entries[idx + 1],
                &attrs->entries[idx],
                tail_entries * sizeof(attr_entry_t));
    }

    /* Fill new directory entry. */
    attr_entry_t *e = &attrs->entries[idx];
    e->key    = key;
    e->type   = type;
    e->size   = size;
    e->offset = attrs->used;
    e->_pad   = 0;

    /* Append payload. */
    memcpy(&attrs->payload[attrs->used], data, size);
    attrs->used += size;
    attrs->count++;

    return true;
}

/* ----------------------------------------------------------------------- */
/* remove                                                                    */
/* ----------------------------------------------------------------------- */

bool entity_attrs_remove(entity_attrs_t *attrs, uint16_t key)
{
    uint16_t idx;
    if (!entity_attrs_search(attrs->entries, attrs->count, key, &idx)) {
        return false;
    }

    /* Remove payload bytes. */
    uint16_t offset = attrs->entries[idx].offset;
    uint8_t  size   = attrs->entries[idx].size;
    compact_payload(attrs, offset, size);

    /* Remove directory entry. */
    uint16_t tail_entries = attrs->count - idx - 1;
    if (tail_entries > 0) {
        memmove(&attrs->entries[idx],
                &attrs->entries[idx + 1],
                tail_entries * sizeof(attr_entry_t));
    }
    attrs->count--;

    return true;
}

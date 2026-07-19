/**
 * @file asset_stream_priority.c
 * @brief Priority / LRU mutation for the streaming manager (rpg-nbp2).
 */
#include "asset_stream_internal.h"

bool fr_asset_stream_set_priority(fr_asset_stream_t *s, uint64_t id, int priority)
{
    fr_asset_slot_t *slot = fr_asset_slot_find(s, id);
    if (slot == NULL) return false;
    slot->priority = priority;
    return true;
}

bool fr_asset_stream_touch(fr_asset_stream_t *s, uint64_t id)
{
    fr_asset_slot_t *slot = fr_asset_slot_find(s, id);
    if (slot == NULL) return false;
    slot->last_used = ++s->clock;
    return true;
}

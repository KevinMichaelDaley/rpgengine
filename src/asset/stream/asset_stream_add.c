/**
 * @file asset_stream_add.c
 * @brief Register / unregister assets in the streaming manager (rpg-nbp2).
 */
#include <string.h>

#include "asset_stream_internal.h"

bool fr_asset_stream_add(fr_asset_stream_t *s, uint64_t id, fr_asset_class_t cls,
                         size_t ram_size, size_t vram_size, int priority,
                         void *slot_user)
{
    if (s == NULL || s->slots == NULL) return false;
    if (fr_asset_slot_find(s, id) != NULL) return false;   /* no duplicates */

    /* First free slot (addresses are stable; freed slots are reused in place). */
    fr_asset_slot_t *slot = NULL;
    for (uint32_t i = 0; i < s->cfg.capacity; ++i) {
        if (!s->slots[i].used) { slot = &s->slots[i]; break; }
    }
    if (slot == NULL) return false;   /* at capacity */

    memset(slot, 0, sizeof *slot);
    slot->used = true;
    slot->id = id;
    slot->cls = cls;
    slot->priority = priority;
    slot->ram_size = ram_size;
    slot->vram_size = vram_size;
    slot->residency = FR_RESIDENCY_ABSENT;
    slot->user = slot_user;
    slot->owner = s;
    slot->last_used = ++s->clock;
    atomic_init(&slot->done, 0);
    s->count++;
    return true;
}

bool fr_asset_stream_remove(fr_asset_stream_t *s, uint64_t id)
{
    fr_asset_slot_t *slot = fr_asset_slot_find(s, id);
    if (slot == NULL) return false;
    if (slot->residency == FR_RESIDENCY_LOADING) return false; /* can't drop in-flight */

    fr_asset_stream_release_vram(s, slot);
    fr_asset_stream_release_ram(s, slot);
    slot->used = false;
    slot->id = 0;
    s->count--;
    return true;
}

/**
 * @file asset_stream_slot.c
 * @brief Slot lookup + tier-release helpers for the streaming manager (rpg-nbp2).
 */
#include "asset_stream_internal.h"

fr_asset_slot_t *fr_asset_slot_find(fr_asset_stream_t *s, uint64_t id)
{
    if (s == NULL || s->slots == NULL) return NULL;
    for (uint32_t i = 0; i < s->cfg.capacity; ++i) {
        if (s->slots[i].used && s->slots[i].id == id) return &s->slots[i];
    }
    return NULL;
}

void fr_asset_stream_release_ram(fr_asset_stream_t *s, fr_asset_slot_t *slot)
{
    if (slot->ram_loaded > 0) {
        if (s->cfg.cbs.evict != NULL)
            s->cfg.cbs.evict(s->cfg.user, slot->id, slot->cls, slot->user,
                             FR_ASSET_DROP_RAM);
        s->ram_used -= slot->ram_loaded;
        slot->ram_loaded = 0;
    }
}

void fr_asset_stream_release_vram(fr_asset_stream_t *s, fr_asset_slot_t *slot)
{
    if (slot->residency == FR_RESIDENCY_VRAM) {
        if (s->cfg.cbs.evict != NULL)
            s->cfg.cbs.evict(s->cfg.user, slot->id, slot->cls, slot->user,
                             FR_ASSET_DROP_VRAM);
        s->vram_used -= slot->vram_size;
    }
}

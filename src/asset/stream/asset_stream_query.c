/**
 * @file asset_stream_query.c
 * @brief Read-only introspection of the streaming manager (rpg-nbp2).
 */
#include "asset_stream_internal.h"

fr_asset_residency_t fr_asset_stream_residency(const fr_asset_stream_t *s, uint64_t id)
{
    fr_asset_slot_t *slot = fr_asset_slot_find((fr_asset_stream_t *)s, id);
    return (slot != NULL) ? slot->residency : FR_RESIDENCY_ABSENT;
}

size_t fr_asset_stream_ram_used(const fr_asset_stream_t *s)
{
    return (s != NULL) ? s->ram_used : 0u;
}

size_t fr_asset_stream_vram_used(const fr_asset_stream_t *s)
{
    return (s != NULL) ? s->vram_used : 0u;
}

uint32_t fr_asset_stream_resident_count(const fr_asset_stream_t *s)
{
    if (s == NULL || s->slots == NULL) return 0u;
    uint32_t n = 0;
    for (uint32_t i = 0; i < s->cfg.capacity; ++i) {
        if (s->slots[i].used && (s->slots[i].residency == FR_RESIDENCY_RAM ||
                                 s->slots[i].residency == FR_RESIDENCY_VRAM))
            n++;
    }
    return n;
}

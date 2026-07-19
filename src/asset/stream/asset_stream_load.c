/**
 * @file asset_stream_load.c
 * @brief The load-job body run on a job fiber (rpg-nbp2). Touches only the slot's
 *        payload + completion flag, never manager internals, so it is safe to run
 *        off the owner thread.
 */
#include "asset_stream_internal.h"

void fr_asset_stream_load_job(void *ud)
{
    fr_asset_slot_t *slot = (fr_asset_slot_t *)ud;
    fr_asset_stream_t *s = slot->owner;
    size_t bytes = 0;
    if (s->cfg.cbs.load != NULL)
        bytes = s->cfg.cbs.load(s->cfg.user, slot->id, slot->cls, slot->user);
    slot->load_result = bytes;
    slot->load_ok = (bytes > 0);
    atomic_store_explicit(&slot->done, 1, memory_order_release);
}

/**
 * @file asset_stream_tick.c
 * @brief The streaming manager's core step (rpg-nbp2): harvest completed loads,
 *        admit the highest-priority pending assets within the RAM/VRAM budgets,
 *        evicting the lowest-priority residents under pressure.
 */
#include "asset_stream_internal.h"
#include "ferrum/job/system.h"

/* Highest-priority ABSENT slot (tie => oldest request first). NULL if none. */
static fr_asset_slot_t *pick_absent(fr_asset_stream_t *s)
{
    fr_asset_slot_t *best = NULL;
    for (uint32_t i = 0; i < s->cfg.capacity; ++i) {
        fr_asset_slot_t *c = &s->slots[i];
        if (!c->used || c->residency != FR_RESIDENCY_ABSENT) continue;
        if (best == NULL || c->priority > best->priority ||
            (c->priority == best->priority && c->last_used < best->last_used))
            best = c;
    }
    return best;
}

/* Lowest-priority RAM-resident slot strictly below max_pri (tie => LRU). */
static fr_asset_slot_t *pick_ram_victim(fr_asset_stream_t *s, int max_pri)
{
    fr_asset_slot_t *v = NULL;
    for (uint32_t i = 0; i < s->cfg.capacity; ++i) {
        fr_asset_slot_t *c = &s->slots[i];
        if (!c->used || c->ram_loaded == 0 || c->priority >= max_pri) continue;
        if (v == NULL || c->priority < v->priority ||
            (c->priority == v->priority && c->last_used < v->last_used))
            v = c;
    }
    return v;
}

/* Lowest-priority VRAM-resident slot strictly below max_pri (tie => LRU). */
static fr_asset_slot_t *pick_vram_victim(fr_asset_stream_t *s, int max_pri)
{
    fr_asset_slot_t *v = NULL;
    for (uint32_t i = 0; i < s->cfg.capacity; ++i) {
        fr_asset_slot_t *c = &s->slots[i];
        if (!c->used || c->residency != FR_RESIDENCY_VRAM || c->priority >= max_pri)
            continue;
        if (v == NULL || c->priority < v->priority ||
            (c->priority == v->priority && c->last_used < v->last_used))
            v = c;
    }
    return v;
}

/* Make room for @p need RAM bytes for a candidate of @p pri. False if it cannot
 * fit even after evicting everything of lower priority. */
static bool ensure_ram(fr_asset_stream_t *s, size_t need, int pri)
{
    if (s->cfg.ram_budget == 0u) return true;         /* unlimited. */
    if (need > s->cfg.ram_budget) return false;       /* never fits. */
    /* Account for both resident (ram_used) and in-flight reserved bytes; in-flight
     * loads cannot be evicted, so a full reservation legitimately blocks admission. */
    while (s->ram_used + s->ram_reserved + need > s->cfg.ram_budget) {
        fr_asset_slot_t *v = pick_ram_victim(s, pri);
        if (v == NULL) return false;
        fr_asset_stream_release_ram(s, v);
        v->residency = FR_RESIDENCY_ABSENT;           /* re-queueable. */
    }
    return true;
}

/* Try to promote a RAM-resident GPU asset into VRAM (frees the RAM copy). */
static void try_upload(fr_asset_stream_t *s, fr_asset_slot_t *slot)
{
    if (s->cfg.vram_budget == 0u || s->cfg.cbs.upload == NULL || slot->vram_size == 0u)
        return;                                        /* VRAM tier disabled. */
    if (slot->vram_size > s->cfg.vram_budget) return;  /* never fits: stay RAM. */
    while (s->vram_used + slot->vram_size > s->cfg.vram_budget) {
        fr_asset_slot_t *v = pick_vram_victim(s, slot->priority);
        if (v == NULL) return;                         /* no room: stay RAM. */
        fr_asset_stream_release_vram(s, v);
        v->residency = FR_RESIDENCY_ABSENT;
    }
    size_t vbytes = s->cfg.cbs.upload(s->cfg.user, slot->id, slot->cls, slot->user);
    if (vbytes > 0) {
        s->vram_used += vbytes;
        slot->vram_size = vbytes;                      /* charge actual. */
        fr_asset_stream_release_ram(s, slot);          /* transient decode freed. */
        slot->residency = FR_RESIDENCY_VRAM;
    }
}

/* Harvest completed loads into RAM (and onward to VRAM). */
static void harvest(fr_asset_stream_t *s)
{
    for (uint32_t i = 0; i < s->cfg.capacity; ++i) {
        fr_asset_slot_t *c = &s->slots[i];
        if (!c->used || c->residency != FR_RESIDENCY_LOADING) continue;
        if (atomic_load_explicit(&c->done, memory_order_acquire) == 0) continue;
        s->in_flight--;
        s->ram_reserved -= c->ram_size;   /* reservation resolved. */
        atomic_store_explicit(&c->done, 0, memory_order_relaxed);
        if (c->load_ok) {
            c->ram_loaded = c->load_result;
            s->ram_used += c->ram_loaded;
            c->residency = FR_RESIDENCY_RAM;
            c->last_used = ++s->clock;
            try_upload(s, c);
        } else {
            c->residency = FR_RESIDENCY_ERROR;
        }
    }
}

/* Admit the highest-priority pending assets that fit, up to max_in_flight. */
static void admit(fr_asset_stream_t *s)
{
    while (s->in_flight < s->cfg.max_in_flight) {
        fr_asset_slot_t *cand = pick_absent(s);
        if (cand == NULL) break;
        if (!ensure_ram(s, cand->ram_size, cand->priority)) break; /* top can't fit. */
        cand->residency = FR_RESIDENCY_LOADING;
        cand->last_used = ++s->clock;
        atomic_store_explicit(&cand->done, 0, memory_order_relaxed);
        s->ram_reserved += cand->ram_size;   /* reserve until the load lands. */
        s->in_flight++;
        if (s->cfg.jobs != NULL)
            job_dispatch(s->cfg.jobs, fr_asset_stream_load_job, cand, 0, NULL);
        else
            fr_asset_stream_load_job(cand);            /* synchronous inline. */
    }
}

void fr_asset_stream_tick(fr_asset_stream_t *s)
{
    if (s == NULL || s->slots == NULL) return;
    harvest(s);       /* pick up loads finished since last tick. */
    admit(s);         /* dispatch new loads from the top of the priority order. */
    if (s->cfg.jobs == NULL)
        harvest(s);   /* synchronous loads completed inline during admit. */
}

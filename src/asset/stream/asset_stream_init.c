/**
 * @file asset_stream_init.c
 * @brief Streaming manager lifecycle (rpg-nbp2).
 */
#include <stdlib.h>
#include <string.h>

#include "asset_stream_internal.h"

bool fr_asset_stream_init(fr_asset_stream_t *s, const fr_asset_stream_config_t *cfg)
{
    if (s == NULL || cfg == NULL || cfg->capacity == 0u) return false;
    memset(s, 0, sizeof *s);
    s->cfg = *cfg;
    if (s->cfg.max_in_flight == 0u) s->cfg.max_in_flight = 1u;
    s->slots = calloc(cfg->capacity, sizeof(fr_asset_slot_t));
    if (s->slots == NULL) return false;
    return true;
}

void fr_asset_stream_destroy(fr_asset_stream_t *s)
{
    if (s == NULL) return;
    free(s->slots);
    s->slots = NULL;
    s->count = 0;
}

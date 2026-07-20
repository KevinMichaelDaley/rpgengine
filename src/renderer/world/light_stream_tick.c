/**
 * @file light_stream_tick.c
 * @brief client_light_stream per-frame drive + residency query (rpg-oda7).
 */
#include <string.h>

#include "light_stream_internal.h"

/** Priority pin for an on-screen lightmap chunk: dominates distance priority
 * (set_interest emits -(dist^2*scale) <= 0) so visible chunks always win a layer. */
#define CLIENT_LM_VISIBLE_PIN 1000000

void client_light_stream_set_visible(client_light_stream_t *ls, const uint8_t *vis, int n)
{
    if (ls == NULL || ls->lm_visible == NULL) return;
    uint32_t cap = ls->n_chunks;
    if (vis == NULL || n <= 0) { memset(ls->lm_visible, 0, cap); return; }
    uint32_t k = ((uint32_t)n < cap) ? (uint32_t)n : cap;
    memcpy(ls->lm_visible, vis, k);
    if (k < cap) memset(ls->lm_visible + k, 0, cap - k);
}

void client_light_stream_tick(client_light_stream_t *ls, const float cam_pos[3])
{
    if (ls == NULL) return;
    /* Distance-priority chunk interest from the camera: nearer chunks stream
     * first / stay resident. */
    if (cam_pos != NULL) fr_chunk_table_set_interest(&ls->table, cam_pos, 1.0f);
    /* On-screen chunks (dual prepass) pin ABOVE distance so residency follows what
     * the camera sees -- the gate that lets chunk count exceed resident layers. */
    if (ls->lm_visible != NULL)
        for (uint32_t c = 0; c < ls->n_chunks; ++c)
            if (ls->lm_visible[c]) fr_asset_stream_set_priority(&ls->stream, c, CLIENT_LM_VISIBLE_PIN);
    /* One streaming step: harvest completed decodes, admit + upload the top
     * chunks within budget (upload runs here on the render thread), evict the rest. */
    fr_asset_stream_tick(&ls->stream);
    /* SDF chunks page disk->RAM on demand (streamed mode); gi_runtime GPU-pages
     * the RAM-resident set each frame via the borrowed stream. */
    if (ls->sdf_streamed) fr_asset_stream_tick(&ls->sdf_stream);
}

int client_light_stream_mesh_layer(const client_light_stream_t *ls, uint32_t mesh_idx)
{
    if (ls == NULL || mesh_idx >= ls->n_meshes) return -1;
    int chunk = ls->mchunk[mesh_idx];
    if (chunk < 0 || (uint32_t)chunk >= ls->n_chunks) return -1;
    const lm_chunk_slot_t *slots = ls->slots;
    return slots[chunk].layer;   /* resident SH-array layer, or -1 if not resident */
}

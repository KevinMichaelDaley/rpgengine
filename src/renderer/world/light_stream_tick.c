/**
 * @file light_stream_tick.c
 * @brief client_light_stream per-frame drive + residency query (rpg-oda7).
 */
#include <string.h>

#include "light_stream_internal.h"

/** Priority pin for an on-screen lightmap chunk: dominates distance priority
 * (set_interest emits -(dist^2*scale) <= 0) so visible chunks always win a layer. */
#define CLIENT_LM_VISIBLE_PIN 1000000

/* Copy an on-screen mask into a [cap] destination (clamped, zero-padded). */
static void set_mask(uint8_t *dst, uint32_t cap, const uint8_t *vis, int n)
{
    if (dst == NULL) return;
    if (vis == NULL || n <= 0) { memset(dst, 0, cap); return; }
    uint32_t k = ((uint32_t)n < cap) ? (uint32_t)n : cap;
    memcpy(dst, vis, k);
    if (k < cap) memset(dst + k, 0, cap - k);
}

void client_light_stream_set_visible(client_light_stream_t *ls, const uint8_t *vis, int n)
{
    if (ls != NULL) set_mask(ls->lm_visible, ls->n_chunks, vis, n);
}

void client_light_stream_set_sdf_visible(client_light_stream_t *ls, const uint8_t *vis, int n)
{
    if (ls != NULL && ls->sdf_streamed) set_mask(ls->sdf_visible, (uint32_t)ls->sdf.n_chunks, vis, n);
}

void client_light_stream_tick(client_light_stream_t *ls, const float cam_pos[3])
{
    if (ls == NULL) return;
    /* Distance-priority interest for BOTH chunk classes from the camera (nearer
     * streams first / stays resident), over the ONE shared budget. */
    if (cam_pos != NULL) {
        fr_chunk_table_set_interest(&ls->table, cam_pos, 1.0f);
        if (ls->sdf_streamed) fr_chunk_table_set_interest(&ls->sdf_table, cam_pos, 1.0f);
    }
    /* On-screen chunks (dual prepass) pin ABOVE distance so residency follows what
     * the camera sees -- lets chunk count exceed resident layers / budget. */
    if (ls->lm_visible != NULL)
        for (uint32_t c = 0; c < ls->n_chunks; ++c)
            if (ls->lm_visible[c]) fr_asset_stream_set_priority(&ls->stream, c, CLIENT_LM_VISIBLE_PIN);
    if (ls->sdf_streamed && ls->sdf_visible != NULL)
        for (int c = 0; c < ls->sdf.n_chunks; ++c)
            if (ls->sdf_visible[c])
                fr_asset_stream_set_priority(&ls->stream, CLIENT_SDF_ID_BASE + (uint32_t)c,
                                             CLIENT_LM_VISIBLE_PIN);
    /* One streaming step drives the whole unified budget: harvest decodes, admit +
     * upload the top-priority chunks (both classes) within RAM/VRAM, evict the rest. */
    fr_asset_stream_tick(&ls->stream);
}

int client_light_stream_mesh_layer(const client_light_stream_t *ls, uint32_t mesh_idx)
{
    if (ls == NULL || mesh_idx >= ls->n_meshes) return -1;
    int chunk = ls->mchunk[mesh_idx];
    if (chunk < 0 || (uint32_t)chunk >= ls->n_chunks) return -1;
    const lm_chunk_slot_t *slots = ls->slots;
    return slots[chunk].layer;   /* resident SH-array layer, or -1 if not resident */
}

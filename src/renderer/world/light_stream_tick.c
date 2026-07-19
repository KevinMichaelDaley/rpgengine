/**
 * @file light_stream_tick.c
 * @brief client_light_stream per-frame drive + residency query (rpg-oda7).
 */
#include "light_stream_internal.h"

void client_light_stream_tick(client_light_stream_t *ls, const float cam_pos[3])
{
    if (ls == NULL) return;
    /* Distance-priority chunk interest from the camera (visibility gating is the
     * dual-index prepass, rpg-sazm). Nearer chunks stream first / stay resident. */
    if (cam_pos != NULL) fr_chunk_table_set_interest(&ls->table, cam_pos, 1.0f);
    /* One streaming step: harvest completed decodes, admit + upload the top
     * chunks within budget (upload runs here on the render thread), evict the rest. */
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

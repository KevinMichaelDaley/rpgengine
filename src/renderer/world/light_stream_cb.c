/**
 * @file light_stream_cb.c
 * @brief fr_asset_stream callbacks for the client light-data streamer (rpg-oda7):
 *        decode a lightmap SH chunk on a job fiber (load), upload it into a
 *        resident SH-array layer on the render thread (upload), and release the
 *        layer + RAM on eviction (evict).
 */
#include <glad/glad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/asset/asset_stream_config.h"  /* FR_ASSET_DROP_* */
#include "light_stream_internal.h"

/* JOB FIBER: read one SDF chunk's distance+albedo into host RAM (no GL). */
size_t client_sdf_load(void *user, uint64_t id, fr_asset_class_t cls, void *slot_user)
{
    (void)user; (void)id; (void)cls;
    sdf_chunk_slot_t *s = slot_user;
    return gi_sdf_stream_chunk_load(&s->owner->sdf, s->chunk, s->owner->sdf_prefix);
}

/* OWNER THREAD: free an evicted SDF chunk's RAM (+ its GPU slot). */
void client_sdf_evict(void *user, uint64_t id, fr_asset_class_t cls, void *slot_user, int drop)
{
    (void)user; (void)id; (void)cls;
    if (drop & FR_ASSET_DROP_RAM) {
        sdf_chunk_slot_t *s = slot_user;
        gi_sdf_stream_chunk_evict(&s->owner->sdf, s->chunk);
    }
}

/* JOB FIBER: decode the chunk's 9 SH coefficient images into slot RAM. No GL.
 * Returns RAM bytes loaded (0 = failure => the slot stays ABSENT). */
size_t client_ls_load(void *user, uint64_t id, fr_asset_class_t cls, void *slot_user)
{
    (void)user; (void)id; (void)cls;
    lm_chunk_slot_t *s = slot_user;
    FILE *f = fopen(s->path, "rb");
    if (f == NULL) return 0;

    char mg[4];
    uint32_t aw = 0, ah = 0, nc = 0, nmh = 0;
    if (fread(mg, 1, 4, f) != 4 || memcmp(mg, "FLM1", 4) != 0 ||
        fread(&aw, 4, 1, f) != 1 || fread(&ah, 4, 1, f) != 1 ||
        fread(&nc, 4, 1, f) != 1 || fread(&nmh, 4, 1, f) != 1 ||
        aw == 0 || ah == 0) {
        fclose(f);
        return 0;
    }
    size_t cpix = (size_t)aw * ah * 3u;
    bool ok = true;
    for (int c = 0; c < 9 && ok; ++c) {
        s->coeff[c] = malloc(cpix * sizeof(float));
        if (s->coeff[c] == NULL || fread(s->coeff[c], sizeof(float), cpix, f) != cpix)
            ok = false;
    }
    fclose(f);
    if (!ok) {
        for (int c = 0; c < 9; ++c) { free(s->coeff[c]); s->coeff[c] = NULL; }
        return 0;
    }
    s->w = (int)aw; s->h = (int)ah;
    return lm_chunk_bytes(s->w, s->h);
}

/* RENDER THREAD (from fr_asset_stream_tick): upload the decoded coeffs into a
 * free SH-array layer, free the RAM copy, and return VRAM bytes. 0 = no free
 * layer (stays RAM-resident; picked up a later tick after an eviction). */
size_t client_ls_upload(void *user, uint64_t id, fr_asset_class_t cls, void *slot_user)
{
    (void)id; (void)cls;
    client_light_stream_t *ls = user;
    lm_chunk_slot_t *s = slot_user;
    if (s->coeff[0] == NULL) return 0;   /* nothing decoded */

    int layer = -1;
    for (uint32_t l = 0; l < ls->n_layers; ++l)
        if (ls->layer_chunk[l] < 0) { layer = (int)l; break; }
    if (layer < 0) return 0;             /* all layers busy this tick */

    for (int c = 0; c < 9; ++c) {
        glBindTexture(GL_TEXTURE_2D_ARRAY, ls->sh_tex[c]);
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer,
                        (GLsizei)s->w, (GLsizei)s->h, 1, GL_RGB, GL_FLOAT, s->coeff[c]);
    }
    ls->layer_chunk[layer] = (int)s->chunk_id;
    s->layer = layer;
    for (int c = 0; c < 9; ++c) { free(s->coeff[c]); s->coeff[c] = NULL; }
    return lm_chunk_bytes(s->w, s->h);
}

/* RENDER THREAD: release the GPU layer and/or the RAM copy per the drop mask. */
void client_ls_evict(void *user, uint64_t id, fr_asset_class_t cls, void *slot_user, int drop)
{
    (void)id; (void)cls;
    client_light_stream_t *ls = user;
    lm_chunk_slot_t *s = slot_user;
    if ((drop & FR_ASSET_DROP_VRAM) && s->layer >= 0) {
        if ((uint32_t)s->layer < ls->n_layers) ls->layer_chunk[s->layer] = -1;
        s->layer = -1;
    }
    if (drop & FR_ASSET_DROP_RAM)
        for (int c = 0; c < 9; ++c) { free(s->coeff[c]); s->coeff[c] = NULL; }
}

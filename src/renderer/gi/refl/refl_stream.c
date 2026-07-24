/**
 * @file refl_stream.c
 * @brief Streamed reflection-probe residency (see refl_stream.h).
 */
#include "ferrum/renderer/gi/refl_stream.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/renderer/gi/gi_sdf_stream.h"
#include "ferrum/renderer/gi/refl_index.h"
#include "ferrum/renderer/gl_constants.h"

#define RS_LOAD(dst, name)                                                    \
    do {                                                                      \
        void *p_ = loader->get_proc_address((name), loader->user_data);       \
        if (p_ == NULL) return false;                                         \
        memcpy(&(dst), &p_, sizeof(p_));                                      \
    } while (0)

bool refl_stream_init(refl_stream_t *rs, const gl_loader_t *loader,
                      uint32_t slot_capacity, uint32_t tile_res,
                      uint32_t mips, uint32_t depth_res)
{
    if (rs == NULL)
        return false;
    memset(rs, 0, sizeof(*rs));
    rs->gain = 1.0f;
    rs->range = 12.0f;
    if (loader == NULL || loader->get_proc_address == NULL ||
        slot_capacity == 0u || tile_res < 8u || mips == 0u ||
        mips > REFL_PROBE_MAX_MIPS)
        return false;

    void (*glGenTextures)(int32_t, uint32_t *);
    void (*glTexImage2D)(uint32_t, int32_t, int32_t, int32_t, int32_t,
                         int32_t, uint32_t, uint32_t, const void *);
    void (*glTexParameteri)(uint32_t, uint32_t, int32_t);
    void (*glGenBuffers)(int32_t, uint32_t *);
    void (*glTexBuffer)(uint32_t, uint32_t, uint32_t);
    RS_LOAD(glGenTextures, "glGenTextures");
    RS_LOAD(glTexImage2D, "glTexImage2D");
    RS_LOAD(glTexParameteri, "glTexParameteri");
    RS_LOAD(glGenBuffers, "glGenBuffers");
    RS_LOAD(glTexBuffer, "glTexBuffer");
    RS_LOAD(rs->glActiveTexture, "glActiveTexture");
    RS_LOAD(rs->glBindTexture, "glBindTexture");
    RS_LOAD(rs->glDeleteTextures, "glDeleteTextures");
    RS_LOAD(rs->glDeleteBuffers, "glDeleteBuffers");
    RS_LOAD(rs->glBindBuffer, "glBindBuffer");
    RS_LOAD(rs->glBufferData, "glBufferData");
    RS_LOAD(rs->glTexSubImage2D, "glTexSubImage2D");

    rs->slot_capacity = slot_capacity;
    rs->tile_res = tile_res;
    rs->mips = mips;
    rs->depth_res = depth_res;
    rs->slots_x = 1u;
    while (rs->slots_x * rs->slots_x < slot_capacity)
        rs->slots_x += 1u;
    rs->slots_y = (slot_capacity + rs->slots_x - 1u) / rs->slots_x;

    /* Fixed CPU pools (load-time only). */
    rs->pool_links =
        (uint16_t *)malloc(slot_capacity * sizeof(uint16_t));
    rs->meta = (float *)calloc((size_t)slot_capacity * 8u, sizeof(float));
    rs->probe_pos =
        (float *)calloc((size_t)slot_capacity * 3u, sizeof(float));
    rs->probe_ao = (float *)calloc(slot_capacity, sizeof(float));
    rs->slot_live = (uint8_t *)calloc(slot_capacity, sizeof(uint8_t));
    rs->cells = (int32_t *)malloc((size_t)REFL_STREAM_MAX_CELLS *
                                  REFL_INDEX_PER_CELL * sizeof(int32_t));
    if (rs->pool_links == NULL || rs->meta == NULL ||
        rs->probe_pos == NULL || rs->probe_ao == NULL ||
        rs->slot_live == NULL || rs->cells == NULL) {
        refl_stream_destroy(rs);
        return false;
    }
    refl_slot_pool_init(&rs->pool, rs->pool_links, slot_capacity);

    /* Colour atlas: explicit prefiltered levels, trilinear. */
    glGenTextures(1, &rs->atlas);
    rs->glBindTexture(GL_TEXTURE_2D, rs->atlas);
    for (uint32_t m = 0; m < mips; ++m) {
        uint32_t tr = tile_res >> m;
        if (tr == 0u) tr = 1u;
        glTexImage2D(GL_TEXTURE_2D, (int32_t)m, GL_RGBA16F,
                     (int32_t)(rs->slots_x * tr),
                     (int32_t)(rs->slots_y * tr), 0, GL_RGBA, GL_FLOAT,
                     NULL);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,
                    (int32_t)mips - 1);

    /* Visibility-depth atlas (single level). */
    if (depth_res > 0u) {
        glGenTextures(1, &rs->depth_tex);
        rs->glBindTexture(GL_TEXTURE_2D, rs->depth_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F,
                     (int32_t)(rs->slots_x * depth_res),
                     (int32_t)(rs->slots_y * depth_res), 0, GL_RG,
                     GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                        GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                        GL_CLAMP_TO_EDGE);
    }

    /* Meta + index TBOs (buffers refilled on residency change). */
    glGenBuffers(1, &rs->meta_buf);
    rs->glBindBuffer(GL_TEXTURE_BUFFER, rs->meta_buf);
    rs->glBufferData(GL_TEXTURE_BUFFER,
                     (ptrdiff_t)((size_t)slot_capacity * 8u *
                                 sizeof(float)),
                     NULL, GL_DYNAMIC_DRAW);
    glGenTextures(1, &rs->meta_tex);
    rs->glBindTexture(GL_TEXTURE_BUFFER, rs->meta_tex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, rs->meta_buf);
    glGenBuffers(1, &rs->index_buf);
    rs->glBindBuffer(GL_TEXTURE_BUFFER, rs->index_buf);
    rs->glBufferData(GL_TEXTURE_BUFFER,
                     (ptrdiff_t)((size_t)REFL_STREAM_MAX_CELLS *
                                 REFL_INDEX_PER_CELL * sizeof(int32_t)),
                     NULL, GL_DYNAMIC_DRAW);
    glGenTextures(1, &rs->index_tex);
    rs->glBindTexture(GL_TEXTURE_BUFFER, rs->index_tex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R32I, rs->index_buf);
    return true;
}

/* Copy one probe's tiles from a chunk payload into an atlas slot. */
static void slot_upload(refl_stream_t *rs,
                        const refl_chunk_payload_t *pl, uint32_t probe,
                        uint32_t slot)
{
    const refl_probe_set_t *set = &pl->set;
    uint32_t sx = slot % rs->slots_x, sy = slot / rs->slots_x;
    for (uint32_t m = 0; m < rs->mips && m < set->mips; ++m) {
        uint32_t tr = rs->tile_res >> m;
        if (tr == 0u) tr = 1u;
        uint32_t src_w = set->tiles_x * tr;
        uint32_t px = (set->probes[probe].tile % set->tiles_x) * tr;
        uint32_t py = (set->probes[probe].tile / set->tiles_x) * tr;
        /* Row-by-row subimage from the payload's atlas region. */
        const float *src = pl->mips[m];
        for (uint32_t y = 0; y < tr; ++y)
            rs->glTexSubImage2D(GL_TEXTURE_2D, (int32_t)m,
                                (int32_t)(sx * tr),
                                (int32_t)(sy * tr + y), (int32_t)tr, 1,
                                GL_RGBA, GL_FLOAT,
                                &src[((size_t)(py + y) * src_w + px) *
                                     4u]);
    }
    if (rs->depth_tex != 0u && pl->depth != NULL &&
        set->depth_res == rs->depth_res) {
        uint32_t dr = rs->depth_res;
        uint32_t src_w = set->tiles_x * dr;
        uint32_t px = (set->probes[probe].tile % set->tiles_x) * dr;
        uint32_t py = (set->probes[probe].tile / set->tiles_x) * dr;
        rs->glBindTexture(GL_TEXTURE_2D, rs->depth_tex);
        for (uint32_t y = 0; y < dr; ++y)
            rs->glTexSubImage2D(GL_TEXTURE_2D, 0, (int32_t)(sx * dr),
                                (int32_t)(sy * dr + y), (int32_t)dr, 1,
                                GL_RG, GL_FLOAT,
                                &pl->depth[((size_t)(py + y) * src_w +
                                            px) * 2u]);
        rs->glBindTexture(GL_TEXTURE_2D, rs->atlas);
    }
}

void refl_stream_sync(refl_stream_t *rs, const struct gi_sdf_stream *sdf)
{
    if (rs == NULL || sdf == NULL || rs->atlas == 0u || !sdf->has_rprobe)
        return;
    /* Late-size the per-chunk bookkeeping to the stream's chunk count. */
    if (rs->chunk_slots == NULL) {
        rs->n_chunks = sdf->n_chunks;
        rs->chunk_slots = (uint16_t **)calloc((size_t)rs->n_chunks,
                                              sizeof(uint16_t *));
        rs->chunk_count = (uint16_t *)calloc((size_t)rs->n_chunks,
                                             sizeof(uint16_t));
        if (rs->chunk_slots == NULL || rs->chunk_count == NULL)
            return;
    }
    rs->glBindTexture(GL_TEXTURE_2D, rs->atlas);
    for (int c = 0; c < rs->n_chunks; ++c) {
        const refl_chunk_payload_t *pl = gi_sdf_stream_chunk_refl(sdf, c);
        if (pl != NULL && rs->chunk_slots[c] == NULL) {
            /* Chunk paged in: take slots. Layout mismatches are skipped
             * (a stale bake against different knobs). */
            if (pl->set.tile_res != rs->tile_res ||
                pl->set.mips > rs->mips) {
                static int warned = 0;
                if (!warned) {
                    warned = 1;
                    fprintf(stderr,
                            "refl_stream: chunk tile layout %ux%u "
                            "mismatches atlas %ux%u -- rebake\n",
                            pl->set.tile_res, pl->set.mips, rs->tile_res,
                            rs->mips);
                }
                continue;
            }
            rs->chunk_slots[c] = (uint16_t *)malloc(
                REFL_CHUNK_MAX_PROBES * sizeof(uint16_t));
            if (rs->chunk_slots[c] == NULL)
                continue;
            uint16_t n = 0u;
            for (uint32_t p = 0; p < pl->set.count; ++p) {
                uint32_t slot = refl_slot_alloc(&rs->pool);
                if (slot == REFL_SLOT_NONE)
                    break;
                slot_upload(rs, pl, p, slot);
                float *t = &rs->meta[(size_t)slot * 8u];
                t[0] = pl->set.probes[p].pos[0];
                t[1] = pl->set.probes[p].pos[1];
                t[2] = pl->set.probes[p].pos[2];
                t[3] = pl->set.probes[p].ao;
                t[4] = (float)(slot % rs->slots_x) / (float)rs->slots_x;
                t[5] = (float)(slot / rs->slots_x) / (float)rs->slots_y;
                t[6] = 1.0f / (float)rs->slots_x;
                t[7] = 1.0f / (float)rs->slots_y;
                rs->probe_pos[(size_t)slot * 3u + 0u] = t[0];
                rs->probe_pos[(size_t)slot * 3u + 1u] = t[1];
                rs->probe_pos[(size_t)slot * 3u + 2u] = t[2];
                rs->probe_ao[slot] = t[3];
                rs->slot_live[slot] = 1u;
                rs->chunk_slots[c][n++] = (uint16_t)slot;
            }
            rs->chunk_count[c] = n;
            rs->dirty = 1;
        } else if (pl == NULL && rs->chunk_slots[c] != NULL) {
            /* Chunk evicted: release its slots. */
            for (uint16_t i = 0; i < rs->chunk_count[c]; ++i) {
                uint32_t slot = rs->chunk_slots[c][i];
                rs->slot_live[slot] = 0u;
                refl_slot_free(&rs->pool, slot);
            }
            free(rs->chunk_slots[c]);
            rs->chunk_slots[c] = NULL;
            rs->chunk_count[c] = 0u;
            rs->dirty = 1;
        }
    }
    if (!rs->dirty)
        return;
    rs->dirty = 0;
    /* Rebuild the meta TBO + the coarse world-grid index over the live
     * slots (dead slots keep stale meta; the index never references
     * them). */
    rs->glBindBuffer(GL_TEXTURE_BUFFER, rs->meta_buf);
    rs->glBufferData(GL_TEXTURE_BUFFER,
                     (ptrdiff_t)((size_t)rs->slot_capacity * 8u *
                                 sizeof(float)),
                     rs->meta, GL_DYNAMIC_DRAW);
    float mn[3] = { 1e30f, 1e30f, 1e30f };
    float mx[3] = { -1e30f, -1e30f, -1e30f };
    refl_probe_t tmp[1];
    refl_probe_set_t live;
    refl_probe_set_init(&live, tmp, 0u);
    (void)live;
    uint32_t n_live = 0u;
    for (uint32_t s = 0; s < rs->slot_capacity; ++s) {
        if (!rs->slot_live[s])
            continue;
        n_live += 1u;
        for (int a = 0; a < 3; ++a) {
            float v = rs->probe_pos[(size_t)s * 3u + a];
            if (v < mn[a]) mn[a] = v;
            if (v > mx[a]) mx[a] = v;
        }
    }
    if (n_live == 0u) {
        rs->idx_dims[0] = rs->idx_dims[1] = rs->idx_dims[2] = 0;
        return;
    }
    /* Cell size chosen so the grid fits the fixed cell budget. */
    float cell = 8.0f;
    for (int tries = 0; tries < 8; ++tries) {
        uint32_t cx = (uint32_t)((mx[0] - mn[0]) / cell) + 1u;
        uint32_t cy = (uint32_t)((mx[1] - mn[1]) / cell) + 1u;
        uint32_t cz = (uint32_t)((mx[2] - mn[2]) / cell) + 1u;
        if ((size_t)cx * cy * cz <= (size_t)REFL_STREAM_MAX_CELLS)
            break;
        cell *= 2.0f;
    }
    /* Build a slot-indexed probe view for refl_index_build. */
    /* The index stores SLOT ids: feed positions via a scratch set whose
     * probe order IS the slot order (dead slots parked far outside). */
    refl_probe_t *scratch = (refl_probe_t *)malloc(
        (size_t)rs->slot_capacity * sizeof(refl_probe_t));
    if (scratch == NULL)
        return;
    refl_probe_set_t view;
    refl_probe_set_init(&view, scratch, rs->slot_capacity);
    for (uint32_t s = 0; s < rs->slot_capacity; ++s) {
        refl_probe_t *p = &scratch[view.count++];
        if (rs->slot_live[s]) {
            p->pos[0] = rs->probe_pos[(size_t)s * 3u + 0u];
            p->pos[1] = rs->probe_pos[(size_t)s * 3u + 1u];
            p->pos[2] = rs->probe_pos[(size_t)s * 3u + 2u];
        } else {
            p->pos[0] = p->pos[1] = p->pos[2] = -1e30f;   /* dropped */
        }
        p->ao = 1.0f;
        p->tile = s;
    }
    refl_index_build(&view, mn, mx, cell, rs->cells,
                     REFL_STREAM_MAX_CELLS, rs->idx_dims);
    free(scratch);
    for (int a = 0; a < 3; ++a)
        rs->idx_origin[a] = mn[a];
    rs->idx_cell = cell;
    rs->glBindBuffer(GL_TEXTURE_BUFFER, rs->index_buf);
    rs->glBufferData(GL_TEXTURE_BUFFER,
                     (ptrdiff_t)((size_t)rs->idx_dims[0] * rs->idx_dims[1] *
                                 rs->idx_dims[2] * REFL_INDEX_PER_CELL *
                                 sizeof(int32_t)),
                     rs->cells, GL_DYNAMIC_DRAW);
}

void refl_stream_bind(const refl_stream_t *rs,
                      shader_uniform_cache_t *cache,
                      const shader_program_t *program)
{
    if (cache == NULL || program == NULL)
        return;
    int on = rs != NULL && rs->atlas != 0u && rs->idx_dims[0] > 0;
    if (on) {
        rs->glActiveTexture(GL_TEXTURE0 + 35u);
        rs->glBindTexture(GL_TEXTURE_2D, rs->atlas);
        rs->glActiveTexture(GL_TEXTURE0 + 41u);
        rs->glBindTexture(GL_TEXTURE_BUFFER, rs->meta_tex);
        rs->glActiveTexture(GL_TEXTURE0 + 42u);
        rs->glBindTexture(GL_TEXTURE_2D, rs->depth_tex);
        rs->glActiveTexture(GL_TEXTURE0 + 43u);
        rs->glBindTexture(GL_TEXTURE_BUFFER, rs->index_tex);
        rs->glActiveTexture(GL_TEXTURE0);
        float org[3] = { rs->idx_origin[0], rs->idx_origin[1],
                         rs->idx_origin[2] };
        float dims[3] = { (float)rs->idx_dims[0], (float)rs->idx_dims[1],
                          (float)rs->idx_dims[2] };
        shader_uniform_set_vec3(cache, program, "u_refl_idx_origin", org);
        shader_uniform_set_vec3(cache, program, "u_refl_idx_dims", dims);
        shader_uniform_set_float(cache, program, "u_refl_idx_cell",
                                 rs->idx_cell);
        shader_uniform_set_float(cache, program, "u_refl_mips",
                                 (float)rs->mips);
        shader_uniform_set_float(cache, program, "u_refl_tile_res",
                                 (float)rs->tile_res);
        shader_uniform_set_float(cache, program, "u_refl_depth_res",
                                 (float)rs->depth_res);
        shader_uniform_set_float(cache, program, "u_refl_gain", rs->gain);
        shader_uniform_set_float(cache, program, "u_refl_range",
                                 rs->range);
    }
    shader_uniform_set_int(cache, program, "u_refl_atlas", 35);
    shader_uniform_set_int(cache, program, "u_refl_meta", 41);
    shader_uniform_set_int(cache, program, "u_refl_depth", 42);
    shader_uniform_set_int(cache, program, "u_refl_index", 43);
    shader_uniform_set_int(cache, program, "u_refl_count", on ? 1 : 0);
}

void refl_stream_destroy(refl_stream_t *rs)
{
    if (rs == NULL)
        return;
    if (rs->glDeleteTextures != NULL) {
        if (rs->atlas) rs->glDeleteTextures(1, &rs->atlas);
        if (rs->depth_tex) rs->glDeleteTextures(1, &rs->depth_tex);
        if (rs->meta_tex) rs->glDeleteTextures(1, &rs->meta_tex);
        if (rs->index_tex) rs->glDeleteTextures(1, &rs->index_tex);
    }
    if (rs->glDeleteBuffers != NULL) {
        if (rs->meta_buf) rs->glDeleteBuffers(1, &rs->meta_buf);
        if (rs->index_buf) rs->glDeleteBuffers(1, &rs->index_buf);
    }
    if (rs->chunk_slots != NULL)
        for (int c = 0; c < rs->n_chunks; ++c)
            free(rs->chunk_slots[c]);
    free(rs->chunk_slots);
    free(rs->chunk_count);
    free(rs->pool_links);
    free(rs->meta);
    free(rs->probe_pos);
    free(rs->probe_ao);
    free(rs->slot_live);
    free(rs->cells);
    memset(rs, 0, sizeof(*rs));
}

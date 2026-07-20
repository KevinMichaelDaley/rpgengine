/**
 * @file light_stream_init.c
 * @brief client_light_stream init + destroy (rpg-oda7): read the lightmap
 *        manifest, create the SH pages, register chunks with the asset streamer.
 */
#include <glad/glad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/renderer/light_stream.h"
#include "ferrum/asset/asset_stream_config.h"
#include "light_stream_internal.h"

/* Read the single-atlas .flm header (FLM1, aw, ah, nc, nmh) + the per-mesh atlas
 * rects that trail the 9 coeff images. Fills atlas dims + mrect[]/mchunk[]. This
 * is metadata only (no coeff pixels) -- the manifest read, cheap. Returns true. */
static bool read_single_atlas_manifest(client_light_stream_t *ls, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return false;
    char mg[4];
    uint32_t aw = 0, ah = 0, nc = 0, nmh = 0;
    if (fread(mg, 1, 4, f) != 4 || memcmp(mg, "FLM1", 4) != 0 ||
        fread(&aw, 4, 1, f) != 1 || fread(&ah, 4, 1, f) != 1 ||
        fread(&nc, 4, 1, f) != 1 || fread(&nmh, 4, 1, f) != 1 ||
        aw == 0 || ah == 0) {
        fclose(f);
        return false;
    }
    ls->atlas.width = aw; ls->atlas.height = ah;
    /* Rects follow the 9 coeff images: header(20) + 9*aw*ah*3 floats. */
    long rects_off = 20L + (long)((size_t)9u * aw * ah * 3u * sizeof(float));
    if (fseek(f, rects_off, SEEK_SET) == 0) {
        for (uint32_t i = 0; i < nmh && i < ls->n_meshes; ++i) {
            lm_atlas_rect_t r;
            if (fread(&r, sizeof r, 1, f) != 1) break;
            ls->mrect[i] = r;
        }
    }
    fclose(f);
    for (uint32_t i = 0; i < ls->n_meshes; ++i) ls->mchunk[i] = 0; /* single chunk. */
    return true;
}

bool client_light_stream_init(client_light_stream_t *ls,
                              const client_light_stream_config_t *cfg,
                              const float scene_min[3], const float scene_max[3])
{
    if (ls == NULL || cfg == NULL || cfg->loader == NULL ||
        cfg->lm_prefix == NULL || cfg->base_dir == NULL) return false;
    memset(ls, 0, sizeof *ls);
    ls->loader = cfg->loader;
    ls->n_meshes = cfg->n_meshes;

    /* Single-atlas == one chunk (the general chunked system's small case). */
    ls->n_chunks = 1u;
    ls->mrect = calloc(cfg->n_meshes ? cfg->n_meshes : 1, sizeof *ls->mrect);
    ls->mchunk = calloc(cfg->n_meshes ? cfg->n_meshes : 1, sizeof *ls->mchunk);
    lm_chunk_slot_t *slots = calloc(ls->n_chunks, sizeof *slots);
    ls->slots = slots;
    ls->entries = calloc(ls->n_chunks, sizeof *ls->entries);
    if (!ls->mrect || !ls->mchunk || !slots || !ls->entries) { client_light_stream_destroy(ls); return false; }

    char path[512];
    snprintf(path, sizeof path, "%s/%s", cfg->base_dir, cfg->lm_prefix);
    if (!read_single_atlas_manifest(ls, path)) { client_light_stream_destroy(ls); return false; }

    /* Resident SH layers: one per chunk up to the residency cap. */
    ls->n_layers = ls->n_chunks < CLIENT_LM_MAX_RESIDENT ? ls->n_chunks : CLIENT_LM_MAX_RESIDENT;
    ls->layer_chunk = malloc(ls->n_layers * sizeof(int));
    if (ls->layer_chunk == NULL) { client_light_stream_destroy(ls); return false; }
    for (uint32_t l = 0; l < ls->n_layers; ++l) ls->layer_chunk[l] = -1;

    /* 9 SH coeff pages sized to the (max) chunk atlas, n_layers deep. */
    for (int c = 0; c < 9; ++c) {
        glGenTextures(1, &ls->sh_tex[c]);
        glBindTexture(GL_TEXTURE_2D_ARRAY, ls->sh_tex[c]);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGB32F, (GLsizei)ls->atlas.width,
                     (GLsizei)ls->atlas.height, (GLsizei)ls->n_layers, 0,
                     GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    /* Per-chunk slot (chunk 0 = the whole atlas). */
    slots[0].owner = ls;
    slots[0].chunk_id = 0u;
    slots[0].w = (int)ls->atlas.width;
    slots[0].h = (int)ls->atlas.height;
    slots[0].layer = -1;
    snprintf(slots[0].path, sizeof slots[0].path, "%s", path);

    /* Streamer + chunk table. load runs on a job fiber; upload/evict on the
     * render thread from client_light_stream_tick. */
    fr_asset_stream_config_t sc;
    memset(&sc, 0, sizeof sc);
    sc.jobs = cfg->jobs;
    sc.ram_budget = cfg->ram_budget;
    sc.vram_budget = cfg->vram_budget;
    sc.max_in_flight = 2u;
    sc.capacity = ls->n_chunks + 1u;
    sc.cbs.load = client_ls_load;
    sc.cbs.upload = client_ls_upload;
    sc.cbs.evict = client_ls_evict;
    sc.user = ls;
    if (!fr_asset_stream_init(&ls->stream, &sc)) { client_light_stream_destroy(ls); return false; }

    fr_chunk_table_init(&ls->table, &ls->stream, ls->entries, ls->n_chunks);
    size_t cb = lm_chunk_bytes(slots[0].w, slots[0].h);
    if (!fr_chunk_table_add(&ls->table, 0u, FR_ASSET_LIGHTMAP_CHUNK,
                            scene_min, scene_max, cb, cb, &slots[0])) {
        client_light_stream_destroy(ls); return false;
    }

    /* SDF/voxel chunks: the GI streamer OWNS the SDF stream (gi_runtime borrows it
     * via ext_sdf, rpg-c7fk). SDF_STREAM=on-demand per-chunk disk->RAM residency
     * via a dedicated fr_asset_stream (load on fibers, RAM budget); default =
     * load-all-to-RAM (unchanged). gi_sdf_stream GPU-pages the RAM-resident set. */
    if (cfg->sdf_prefix != NULL && cfg->sdf_prefix[0] != '\0') {
        snprintf(ls->sdf_prefix, sizeof ls->sdf_prefix, "%s/%s", cfg->base_dir, cfg->sdf_prefix);
        /* Per-chunk on-demand SDF residency is the DEFAULT; LEGACY_SDF=1 forces the
         * old load-all-to-RAM path (fallback if the scan fails). */
        if (getenv("LEGACY_SDF") == NULL && gi_sdf_stream_scan(&ls->sdf, ls->sdf_prefix) > 0) {
            ls->has_sdf = 1;
            uint32_t n = (uint32_t)ls->sdf.n_chunks;
            sdf_chunk_slot_t *ss = calloc(n ? n : 1, sizeof *ss);
            ls->sdf_slots = ss;
            fr_asset_stream_config_t sc;
            memset(&sc, 0, sizeof sc);
            sc.jobs = cfg->jobs;
            sc.ram_budget = cfg->ram_budget;
            sc.vram_budget = 0;          /* no VRAM tier -- gi_runtime GPU-pages RAM chunks. */
            sc.max_in_flight = 4u;
            sc.capacity = n + 1u;
            sc.cbs.load = client_sdf_load;
            sc.cbs.evict = client_sdf_evict;
            sc.user = ls;
            if (ss != NULL && fr_asset_stream_init(&ls->sdf_stream, &sc)) {
                ls->sdf_streamed = 1;
                for (uint32_t c = 0; c < n; ++c) {
                    ss[c].owner = ls; ss[c].chunk = (int)c;
                    const int32_t *dm = ls->sdf.ram[c].dims;
                    size_t ram = (size_t)dm[0] * dm[1] * dm[2] * 4u * sizeof(float);
                    fr_asset_stream_add(&ls->sdf_stream, c, FR_ASSET_SDF_CHUNK, ram, 0, 0, &ss[c]);
                }
            }
        } else if (gi_sdf_stream_load(&ls->sdf, ls->sdf_prefix) > 0) {
            ls->has_sdf = 1;
        }
    }
    return true;
}

void client_light_stream_destroy(client_light_stream_t *ls)
{
    if (ls == NULL) return;
    /* Evict residents (frees any RAM copies) before tearing down the table. */
    lm_chunk_slot_t *slots = ls->slots;
    if (slots != NULL) {
        for (uint32_t i = 0; i < ls->n_chunks; ++i)
            for (int c = 0; c < 9; ++c) { free(slots[i].coeff[c]); slots[i].coeff[c] = NULL; }
    }
    if (ls->stream.slots != NULL) fr_asset_stream_destroy(&ls->stream);
    if (ls->sdf_streamed && ls->sdf_stream.slots != NULL) fr_asset_stream_destroy(&ls->sdf_stream);
    free(ls->sdf_slots);
    if (ls->has_sdf) gi_sdf_stream_destroy(&ls->sdf);
    for (int c = 0; c < 9; ++c) if (ls->sh_tex[c]) glDeleteTextures(1, &ls->sh_tex[c]);
    free(ls->slots); free(ls->entries); free(ls->layer_chunk);
    free(ls->mrect); free(ls->mchunk);
    memset(ls, 0, sizeof *ls);
}

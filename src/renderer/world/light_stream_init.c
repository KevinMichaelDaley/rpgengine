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

/* Peek a ZLM1 manifest header. Returns the chunk count (0 => not a ZLM1 file, so
 * the caller uses the single-atlas path) and fills the max atlas dims. */
static uint32_t peek_zlm1_manifest(const char *mpath, uint32_t *aw, uint32_t *ah)
{
    FILE *mf = fopen(mpath, "rb");
    if (mf == NULL) return 0;
    char mg[4]; uint32_t hdr[4];
    uint32_t n = 0;
    if (fread(mg, 1, 4, mf) == 4 && memcmp(mg, "ZLM1", 4) == 0 &&
        fread(hdr, sizeof hdr, 1, mf) == 1 && hdr[1] > 0 && hdr[2] > 0 && hdr[3] > 0) {
        n = hdr[1]; *aw = hdr[2]; *ah = hdr[3];
    }
    fclose(mf);
    return n;
}

/* Read a ZLM1 manifest body into the (already-allocated) per-mesh tables: chunk
 * id + atlas rect per mesh, plus the optional per-chunk world-box trailer. If the
 * trailer is absent, @p boxes is left untouched (caller falls back to scene box).
 * Returns true on a well-formed record section; @p have_boxes signals the trailer. */
static bool read_zlm1_manifest(client_light_stream_t *ls, const char *mpath,
                               uint32_t n_chunks, float *boxes, int *have_boxes)
{
    *have_boxes = 0;
    FILE *mf = fopen(mpath, "rb");
    if (mf == NULL) return false;
    char mg[4]; uint32_t hdr[4];
    if (fread(mg, 1, 4, mf) != 4 || memcmp(mg, "ZLM1", 4) != 0 ||
        fread(hdr, sizeof hdr, 1, mf) != 1) { fclose(mf); return false; }
    uint32_t nm_m = hdr[0];
    for (uint32_t i = 0; i < ls->n_meshes; ++i) { ls->mchunk[i] = -1; ls->mrect[i] = (lm_atlas_rect_t){0,0,0,0}; }
    for (uint32_t i = 0; i < nm_m; ++i) {
        int32_t L; uint32_t r[4];
        if (fread(&L, 4, 1, mf) != 1 || fread(r, 4, 4, mf) != 4) { fclose(mf); return false; }
        if (i < ls->n_meshes) { ls->mchunk[i] = (int)L; ls->mrect[i] = (lm_atlas_rect_t){ r[2], r[3], r[0], r[1] }; }
    }
    /* Optional trailer: n_chunks * (min[3],max[3]). */
    if (fread(boxes, sizeof(float), (size_t)n_chunks * 6u, mf) == (size_t)n_chunks * 6u) *have_boxes = 1;
    fclose(mf);
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

    /* Lightmap base = lm_prefix without a trailing ".flm" (the chunked bake emits
     * <base>_manifest.bin + <base>_cNNN.flm; single-atlas keeps <lm_prefix>). */
    char lmbase[512];
    snprintf(lmbase, sizeof lmbase, "%s", cfg->lm_prefix);
    { size_t bl = strlen(lmbase); if (bl > 4 && strcmp(lmbase + (bl - 4), ".flm") == 0) lmbase[bl - 4] = '\0'; }

    /* Multi-chunk if a ZLM1 manifest exists, else single-atlas (small case). */
    char mpath[512];
    snprintf(mpath, sizeof mpath, "%s/%s_manifest.bin", cfg->base_dir, lmbase);
    uint32_t zaw = 0, zah = 0;
    uint32_t nz = peek_zlm1_manifest(mpath, &zaw, &zah);
    int multichunk = (nz > 0);
    ls->n_chunks = multichunk ? nz : 1u;

    ls->mrect = calloc(cfg->n_meshes ? cfg->n_meshes : 1, sizeof *ls->mrect);
    ls->mchunk = calloc(cfg->n_meshes ? cfg->n_meshes : 1, sizeof *ls->mchunk);
    lm_chunk_slot_t *slots = calloc(ls->n_chunks, sizeof *slots);
    ls->slots = slots;
    ls->entries = calloc(ls->n_chunks, sizeof *ls->entries);
    ls->lm_visible = calloc(ls->n_chunks, 1);   /* on-screen pin mask (dual prepass). */
    float *boxes = calloc((size_t)ls->n_chunks * 6u, sizeof(float));
    if (!ls->mrect || !ls->mchunk || !slots || !ls->entries || !ls->lm_visible || !boxes) {
        free(boxes); client_light_stream_destroy(ls); return false;
    }

    char single_path[512];
    snprintf(single_path, sizeof single_path, "%s/%s", cfg->base_dir, cfg->lm_prefix);
    int have_boxes = 0;
    if (multichunk) {
        ls->atlas.width = zaw; ls->atlas.height = zah;
        if (!read_zlm1_manifest(ls, mpath, ls->n_chunks, boxes, &have_boxes)) {
            free(boxes); client_light_stream_destroy(ls); return false;
        }
        for (uint32_t c = 0; c < ls->n_chunks; ++c) {
            slots[c].owner = ls; slots[c].chunk_id = c;
            slots[c].w = 0; slots[c].h = 0; slots[c].layer = -1;   /* dims filled by load. */
            snprintf(slots[c].path, sizeof slots[c].path, "%s/%s_c%03u.flm", cfg->base_dir, lmbase, c);
        }
        printf("[light_stream] %u lightmap chunks, atlas %ux%u, boxes=%s\n",
               ls->n_chunks, zaw, zah, have_boxes ? "yes" : "scene");
        fflush(stdout);
    } else {
        if (!read_single_atlas_manifest(ls, single_path)) { free(boxes); client_light_stream_destroy(ls); return false; }
        slots[0].owner = ls; slots[0].chunk_id = 0u;
        slots[0].w = (int)ls->atlas.width; slots[0].h = (int)ls->atlas.height; slots[0].layer = -1;
        snprintf(slots[0].path, sizeof slots[0].path, "%s", single_path);
    }

    /* Resident SH layers: min(chunk count, hard cap, VRAM budget / per-layer bytes).
     * The physical SH-array layer count == the stream's VRAM budget below, so the
     * streamer's priority eviction is exactly what frees a layer for a new chunk. */
    size_t layer_bytes = lm_chunk_bytes((int)ls->atlas.width, (int)ls->atlas.height);
    uint32_t layer_cap = CLIENT_LM_MAX_RESIDENT;
    if (cfg->vram_budget > 0 && layer_bytes > 0) {
        uint32_t by_budget = (uint32_t)(cfg->vram_budget / layer_bytes);
        if (by_budget < 1u) by_budget = 1u;              /* always room for at least one. */
        if (by_budget < layer_cap) layer_cap = by_budget;
    }
    ls->n_layers = ls->n_chunks < layer_cap ? ls->n_chunks : layer_cap;
    ls->layer_chunk = malloc(ls->n_layers * sizeof(int));
    if (ls->layer_chunk == NULL) { free(boxes); client_light_stream_destroy(ls); return false; }
    for (uint32_t l = 0; l < ls->n_layers; ++l) ls->layer_chunk[l] = -1;

    /* 9 SH coeff pages sized to the (max) chunk atlas, n_layers deep. */
    for (int c = 0; c < 9; ++c) {
        glGenTextures(1, &ls->sh_tex[c]);
        glBindTexture(GL_TEXTURE_2D_ARRAY, ls->sh_tex[c]);
        /* lm_fp16 halves the SH pages; baked irradiance has ample range for it. */
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, cfg->lm_fp16 ? GL_RGB16F : GL_RGB32F,
                     (GLsizei)ls->atlas.width,
                     (GLsizei)ls->atlas.height, (GLsizei)ls->n_layers, 0,
                     GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    /* Scan the SDF chunks FIRST (headers only -> chunk count + world boxes + GPU
     * pool) so they can share ONE stream + budget with the lightmap (rpg-vfmi).
     * Per-chunk on-demand residency is the DEFAULT; LEGACY_SDF=1 forces the old
     * load-all-to-RAM path (fallback if the scan fails -- then not in the stream). */
    uint32_t n_sdf = 0;
    if (cfg->sdf_prefix != NULL && cfg->sdf_prefix[0] != '\0') {
        snprintf(ls->sdf_prefix, sizeof ls->sdf_prefix, "%s/%s", cfg->base_dir, cfg->sdf_prefix);
        /* Pool sizing + texture format BEFORE the scan/load creates the GPU pool. */
        gi_sdf_stream_configure(&ls->sdf, cfg->sdf_resident_slots, cfg->sdf_fp16);
        if (getenv("LEGACY_SDF") == NULL && gi_sdf_stream_scan(&ls->sdf, ls->sdf_prefix) > 0) {
            ls->has_sdf = 1; ls->sdf_streamed = 1;
            n_sdf = (uint32_t)ls->sdf.n_chunks;
        } else if (gi_sdf_stream_load(&ls->sdf, ls->sdf_prefix) > 0) {
            ls->has_sdf = 1;   /* legacy load-all: resident in RAM, outside the stream. */
        }
    }

    /* ONE stream owns lightmap SH chunks + SDF chunks under a single RAM/VRAM
     * budget (rpg-vfmi). Callbacks dispatch by asset class; load runs on job
     * fibers, upload/evict on the render thread from client_light_stream_tick. The
     * VRAM budget == the physical SH layer capacity so priority eviction frees a
     * layer for a newly-visible chunk; SDF chunks carry vram_size=0 (RAM-only). */
    size_t cb = layer_bytes;
    fr_asset_stream_config_t sc;
    memset(&sc, 0, sizeof sc);
    sc.jobs = cfg->jobs;
    sc.ram_budget = cfg->ram_budget;
    sc.vram_budget = (size_t)ls->n_layers * cb;
    sc.max_in_flight = 4u;
    sc.capacity = ls->n_chunks + n_sdf + 1u;
    sc.cbs.load = client_ls_load;
    sc.cbs.upload = client_ls_upload;
    sc.cbs.evict = client_ls_evict;
    sc.user = ls;
    if (!fr_asset_stream_init(&ls->stream, &sc)) { free(boxes); client_light_stream_destroy(ls); return false; }

    /* Lightmap chunks: ids [0, n_lm) over their world boxes, VRAM tier (vram_size=cb).
     * Single-atlas + trailer-less manifests fall back to the whole scene box. */
    fr_chunk_table_init(&ls->table, &ls->stream, ls->entries, ls->n_chunks);
    for (uint32_t c = 0; c < ls->n_chunks; ++c) {
        const float *bmin = scene_min, *bmax = scene_max;
        if (have_boxes) { bmin = &boxes[c*6]; bmax = &boxes[c*6+3]; }
        if (!fr_chunk_table_add(&ls->table, c, FR_ASSET_LIGHTMAP_CHUNK,
                                bmin, bmax, cb, cb, &slots[c])) {
            free(boxes); client_light_stream_destroy(ls); return false;
        }
    }
    free(boxes);

    /* SDF chunks: ids [CLIENT_SDF_ID_BASE, +n_sdf) over their world boxes in a
     * SECOND chunk table on the SAME stream, RAM-only (vram_size=0). gi_runtime
     * GPU-pages the RAM-resident set (borrows ls->sdf). */
    if (n_sdf > 0) {
        sdf_chunk_slot_t *ss = calloc(n_sdf, sizeof *ss);
        ls->sdf_slots = ss;
        ls->sdf_entries = calloc(n_sdf, sizeof *ls->sdf_entries);
        ls->sdf_visible = calloc(n_sdf, 1);
        float *sbmin = malloc((size_t)n_sdf * 3u * sizeof(float));
        float *sbmax = malloc((size_t)n_sdf * 3u * sizeof(float));
        if (!ss || !ls->sdf_entries || !ls->sdf_visible || !sbmin || !sbmax) {
            free(sbmin); free(sbmax); client_light_stream_destroy(ls); return false;
        }
        gi_sdf_stream_boxes(&ls->sdf, sbmin, sbmax);
        fr_chunk_table_init(&ls->sdf_table, &ls->stream, ls->sdf_entries, n_sdf);
        for (uint32_t c = 0; c < n_sdf; ++c) {
            ss[c].owner = ls; ss[c].chunk = (int)c;
            const int32_t *dm = ls->sdf.ram[c].dims;
            size_t ram = (size_t)dm[0] * dm[1] * dm[2] * 4u * sizeof(float);
            if (!fr_chunk_table_add(&ls->sdf_table, CLIENT_SDF_ID_BASE + c, FR_ASSET_SDF_CHUNK,
                                    &sbmin[c*3], &sbmax[c*3], ram, 0, &ss[c])) {
                free(sbmin); free(sbmax); client_light_stream_destroy(ls); return false;
            }
        }
        free(sbmin); free(sbmax);
    }
    /* Optional streamed probe seed (svol): needs the level descriptor for
     * mesh paths + TRS at extraction time. Failure is non-fatal. */
    if (cfg->svol_desc != NULL)
        (void)client_ls_svol_init(ls, cfg->svol_desc, cfg->base_dir, cfg->jobs);
    return true;
}

void client_light_stream_destroy(client_light_stream_t *ls)
{
    client_ls_svol_destroy(ls);
    if (ls == NULL) return;
    /* Evict residents (frees any RAM copies) before tearing down the table. */
    lm_chunk_slot_t *slots = ls->slots;
    if (slots != NULL) {
        for (uint32_t i = 0; i < ls->n_chunks; ++i)
            for (int c = 0; c < 9; ++c) { free(slots[i].coeff[c]); slots[i].coeff[c] = NULL; }
    }
    if (ls->stream.slots != NULL) fr_asset_stream_destroy(&ls->stream);   /* unified lm+SDF. */
    free(ls->sdf_slots); free(ls->sdf_entries); free(ls->sdf_visible);
    if (ls->has_sdf) gi_sdf_stream_destroy(&ls->sdf);
    for (int c = 0; c < 9; ++c) if (ls->sh_tex[c]) glDeleteTextures(1, &ls->sh_tex[c]);
    free(ls->slots); free(ls->entries); free(ls->layer_chunk);
    free(ls->mrect); free(ls->mchunk); free(ls->lm_visible);
    memset(ls, 0, sizeof *ls);
}

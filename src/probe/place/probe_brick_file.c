/**
 * @file probe_brick_file.c
 * @brief .bricks sidecar save/load (see probe_brick_file.h).
 */
#include <stdio.h>
#include <string.h>

#include "ferrum/memory/arena.h"
#include "ferrum/probe/place/probe_brick_file.h"

#define BRICK_FILE_MAGIC "PBK1"
/* Corruption guard: far above any real placement, far below overflow. */
#define BRICK_FILE_MAX_BRICKS  (4u * 1024u * 1024u)
#define BRICK_FILE_MAX_PROBES  (64u * 1024u * 1024u)

bool probe_brick_data_save(const char *path, const probe_brick_data_t *d)
{
    if (path == NULL || d == NULL || d->bricks == NULL ||
        (d->valid == NULL && d->n_probes > 0))
        return false;
    FILE *f = fopen(path, "wb");
    if (f == NULL) return false;

    bool ok = true;
    ok = ok && fwrite(BRICK_FILE_MAGIC, 1, 4, f) == 4;
    ok = ok && fwrite(&d->n_bricks, sizeof(uint32_t), 1, f) == 1;
    ok = ok && fwrite(&d->n_probes, sizeof(uint32_t), 1, f) == 1;
    ok = ok && fwrite(&d->coarse_brick, sizeof(float), 1, f) == 1;
    ok = ok && fwrite(&d->levels, sizeof(int32_t), 1, f) == 1;
    ok = ok && fwrite(d->aabb_min, sizeof(float), 3, f) == 3;
    ok = ok && fwrite(d->aabb_max, sizeof(float), 3, f) == 3;
    if (ok && d->n_bricks > 0)
        ok = fwrite(d->bricks, sizeof(probe_brick_t), d->n_bricks, f) == d->n_bricks;
    if (ok && d->n_probes > 0)
        ok = fwrite(d->valid, 1, d->n_probes, f) == d->n_probes;
    if (fclose(f) != 0) ok = false;
    return ok;
}

bool probe_brick_data_load(const char *path, struct arena *arena,
                           probe_brick_data_t *out)
{
    if (path == NULL || arena == NULL || out == NULL) return false;
    FILE *f = fopen(path, "rb");
    if (f == NULL) return false;

    probe_brick_data_t d;
    memset(&d, 0, sizeof d);
    char magic[4];
    bool ok = true;
    ok = ok && fread(magic, 1, 4, f) == 4 && memcmp(magic, BRICK_FILE_MAGIC, 4) == 0;
    ok = ok && fread(&d.n_bricks, sizeof(uint32_t), 1, f) == 1;
    ok = ok && fread(&d.n_probes, sizeof(uint32_t), 1, f) == 1;
    ok = ok && fread(&d.coarse_brick, sizeof(float), 1, f) == 1;
    ok = ok && fread(&d.levels, sizeof(int32_t), 1, f) == 1;
    ok = ok && fread(d.aabb_min, sizeof(float), 3, f) == 3;
    ok = ok && fread(d.aabb_max, sizeof(float), 3, f) == 3;
    ok = ok && d.n_bricks <= BRICK_FILE_MAX_BRICKS && d.n_probes <= BRICK_FILE_MAX_PROBES;
    if (ok && d.n_bricks > 0) {
        d.bricks = arena_alloc((arena_t *)arena, 16u,
                               (size_t)d.n_bricks * sizeof(probe_brick_t));
        ok = d.bricks != NULL &&
             fread(d.bricks, sizeof(probe_brick_t), d.n_bricks, f) == d.n_bricks;
    }
    if (ok && d.n_probes > 0) {
        d.valid = arena_alloc((arena_t *)arena, 16u, d.n_probes);
        ok = d.valid != NULL && fread(d.valid, 1, d.n_probes, f) == d.n_probes;
    }
    fclose(f);
    if (!ok) return false;
    *out = d;
    return true;
}

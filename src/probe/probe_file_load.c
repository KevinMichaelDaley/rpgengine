/**
 * @file probe_file_load.c
 * @brief Read a .probes file into a caller arena (rpg-ft0g). See probe_file_save.c
 *        for the layout.
 */
#include <stdio.h>
#include <string.h>

#include "ferrum/memory/arena.h"
#include "ferrum/probe/probe_file.h"

/* Sanity cap so a corrupt count can't request an absurd allocation. */
#define PROBE_FILE_MAX_PROBES  (16u * 1024u * 1024u)
#define PROBE_FILE_MAX_SH      64u

bool probe_set_load(const char *path, struct arena *arena, probe_set_t *out)
{
    if (path == NULL || arena == NULL || out == NULL) return false;
    memset(out, 0, sizeof *out);

    FILE *f = fopen(path, "rb");
    if (f == NULL) return false;

    bool ok = false;
    char magic[4];
    uint32_t count = 0, sh_c = 0;
    if (fread(magic, 1, 4, f) == 4 && memcmp(magic, "PRB1", 4) == 0 &&
        fread(&count, sizeof(uint32_t), 1, f) == 1 &&
        fread(&sh_c, sizeof(uint32_t), 1, f) == 1 &&
        fread(out->grid_origin, sizeof(float), 3, f) == 3 &&
        fread(out->grid_cell, sizeof(float), 3, f) == 3 &&
        fread(out->grid_dim, sizeof(int32_t), 3, f) == 3 &&
        count <= PROBE_FILE_MAX_PROBES && sh_c <= PROBE_FILE_MAX_SH) {

        ok = true;
        if (count > 0) {
            float *pos = arena_alloc((arena_t *)arena, 16u,
                                     (size_t)count * 3u * sizeof(float));
            if (pos == NULL ||
                fread(pos, sizeof(float), (size_t)count * 3u, f) != (size_t)count * 3u) {
                ok = false;
            } else {
                out->positions = pos;
            }
            if (ok && sh_c > 0u) {
                float *sh = arena_alloc((arena_t *)arena, 16u,
                                        (size_t)count * sh_c * sizeof(float));
                if (sh == NULL ||
                    fread(sh, sizeof(float), (size_t)count * sh_c, f) !=
                        (size_t)count * sh_c) {
                    ok = false;
                } else {
                    out->sh = sh;
                }
            }
        }
    }
    if (ok) {
        out->count = count;
        out->sh_coeffs = sh_c;
    } else {
        memset(out, 0, sizeof *out);
    }
    fclose(f);
    return ok;
}

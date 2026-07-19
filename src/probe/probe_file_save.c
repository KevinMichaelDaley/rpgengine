/**
 * @file probe_file_save.c
 * @brief Write a probe set to a .probes file (rpg-ft0g). Layout:
 *   char   magic[4]  = "PRB1"
 *   u32    count
 *   u32    sh_coeffs        (floats of baked SH per probe; 0 = none)
 *   float  origin[3], cell[3]
 *   i32    dim[3]           (0,0,0 = not a regular grid)
 *   float  positions[count*3]
 *   float  sh[count*sh_coeffs]   (only if sh_coeffs>0)
 * Host byte order (matches lm_sdf_file.c style).
 */
#include <stdio.h>

#include "ferrum/probe/probe_file.h"

bool probe_set_save(const char *path, const probe_set_t *set)
{
    if (path == NULL || set == NULL || set->positions == NULL) return false;

    uint32_t sh_c = (set->sh != NULL) ? set->sh_coeffs : 0u;
    FILE *f = fopen(path, "wb");
    if (f == NULL) return false;

    bool ok = true;
    ok = ok && fwrite("PRB1", 1, 4, f) == 4;
    ok = ok && fwrite(&set->count, sizeof(uint32_t), 1, f) == 1;
    ok = ok && fwrite(&sh_c, sizeof(uint32_t), 1, f) == 1;
    ok = ok && fwrite(set->grid_origin, sizeof(float), 3, f) == 3;
    ok = ok && fwrite(set->grid_cell, sizeof(float), 3, f) == 3;
    ok = ok && fwrite(set->grid_dim, sizeof(int32_t), 3, f) == 3;
    if (ok && set->count > 0) {
        ok = fwrite(set->positions, sizeof(float), (size_t)set->count * 3u, f)
             == (size_t)set->count * 3u;
        if (ok && sh_c > 0u)
            ok = fwrite(set->sh, sizeof(float), (size_t)set->count * sh_c, f)
                 == (size_t)set->count * sh_c;
    }
    if (fclose(f) != 0) ok = false;
    return ok;
}

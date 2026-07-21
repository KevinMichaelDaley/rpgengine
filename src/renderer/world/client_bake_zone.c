/**
 * @file client_bake_zone.c
 * @brief Bake-side composition of the GLOBAL low-res zone SDF (see
 *        client_bake_zone_sdf in client_bake.h). Loads the written fine chunks,
 *        min-downsamples them (gi_zone_sdf), and writes "<prefix>_zone.sdf".
 *        One zone per bake today; world zones each get their own when they land.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/renderer/client_bake.h"
#include "ferrum/renderer/gi/gi_zone_sdf.h"
#include "ferrum/lightmap/lm_sdf_file.h"

#define BAKE_ZONE_MAX_CHUNKS 256

bool client_bake_zone_sdf(const char *sdf_prefix, int max_dim)
{
    if (sdf_prefix == NULL || sdf_prefix[0] == '\0' || max_dim < 1) return false;
    if (max_dim > 128) max_dim = 128;

    /* Load every on-disk fine chunk (sparse cNNN allowed, like the runtime scan). */
    lm_sdf_data_t *loaded = malloc((size_t)BAKE_ZONE_MAX_CHUNKS * sizeof *loaded);
    gi_zone_sdf_src_t *srcs = malloc((size_t)BAKE_ZONE_MAX_CHUNKS * sizeof *srcs);
    if (loaded == NULL || srcs == NULL) { free(loaded); free(srcs); return false; }
    uint32_t n = 0;
    for (uint32_t cc = 0; cc < 100000u && n < BAKE_ZONE_MAX_CHUNKS; ++cc) {
        char path[600];
        snprintf(path, sizeof path, "%s_c%03u.sdf", sdf_prefix, cc);
        FILE *f = fopen(path, "rb");
        if (f == NULL) continue;
        fclose(f);
        if (!lm_sdf_load(path, &loaded[n])) continue;
        gi_zone_sdf_src_t *sc = &srcs[n];
        memset(sc, 0, sizeof *sc);
        sc->dist = loaded[n].dist;
        sc->albedo = loaded[n].albedo;
        for (int a = 0; a < 3; ++a) {
            sc->dims[a] = loaded[n].dims[a];
            sc->origin[a] = loaded[n].origin[a];
        }
        sc->voxel = loaded[n].voxel;
        ++n;
    }

    bool ok = false;
    int32_t dims[3]; float vox, org[3];
    if (n > 0 && gi_zone_sdf_plan(srcs, n, max_dim, dims, &vox, org)) {
        uint32_t cells = (uint32_t)dims[0] * (uint32_t)dims[1] * (uint32_t)dims[2];
        float *dist = malloc((size_t)cells * sizeof(float));
        float *alb = malloc((size_t)cells * 3u * sizeof(float));
        if (dist != NULL && alb != NULL &&
            gi_zone_sdf_compose(srcs, n, dims, vox, org, dist, alb, cells)) {
            char zpath[600];
            snprintf(zpath, sizeof zpath, "%s_zone.sdf", sdf_prefix);
            ok = lm_sdf_save_rgba(zpath, dims, vox, org, dist, alb);
            if (ok)
                printf("[client_bake] zone SDF %dx%dx%d @ %.2fm -> %s\n",
                       dims[0], dims[1], dims[2], (double)vox, zpath);
        }
        free(dist); free(alb);
    }

    for (uint32_t i = 0; i < n; ++i) { free(loaded[i].dist); free(loaded[i].albedo); }
    free(loaded); free(srcs);
    return ok;
}

/**
 * @file client_static_volume.c
 * @brief Build the static-irradiance GI volume for the client from the level's
 *        fvma meshes + the baked SH lightmap (rpg-zygg). Folds the baked lightmap
 *        ambience into a coarse 3D world grid the dynamic GI probes gather, so
 *        shadowed interior surfaces read the baked bounce (parity with
 *        hall_lit_dynamic's build_static_irr_volume, adapted to fvma + per-object
 *        TRS instead of world-space dmesh).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/renderer/client_scene.h"
#include "ferrum/scene/scene_desc.h"
#include "ferrum/renderer/gi/gi_static_volume.h"
#include "ferrum/editor/mesh/mesh_vao_format.h"
#include "ferrum/editor/mesh/mesh_slot.h"

/* Column-major model matrix from translation/quaternion(xyzw)/scale. */
static void svol_model(const float p[3], const float q[4], const float s[3], float m[16])
{
    float x = q[0], y = q[1], z = q[2], w = q[3];
    m[0] = (1-2*(y*y+z*z))*s[0]; m[1] = (2*(x*y+z*w))*s[0]; m[2] = (2*(x*z-y*w))*s[0];
    m[4] = (2*(x*y-z*w))*s[1]; m[5] = (1-2*(x*x+z*z))*s[1]; m[6] = (2*(y*z+x*w))*s[1];
    m[8] = (2*(x*z+y*w))*s[2]; m[9] = (2*(y*z-x*w))*s[2]; m[10] = (1-2*(x*x+y*y))*s[2];
    m[12] = p[0]; m[13] = p[1]; m[14] = p[2];
}

/* Read SH coeff layers 0..3 (RGB) of a single-atlas .flm (past the 20-byte FLM1
 * header). Allocates sh[0..3]; caller frees. Returns false on error. */
static bool read_sh0_3(const char *path, uint32_t aw, uint32_t ah, float *sh[4])
{
    for (int c = 0; c < 4; ++c) sh[c] = NULL;
    FILE *lf = fopen(path, "rb");
    if (lf == NULL) return false;
    if (fseek(lf, 20, SEEK_SET) != 0) { fclose(lf); return false; }
    size_t npix = (size_t)aw * ah * 3u;
    for (int c = 0; c < 4; ++c) {
        sh[c] = malloc(npix * sizeof(float));
        if (sh[c] == NULL || fread(sh[c], sizeof(float), npix, lf) != npix) {
            for (int k = 0; k <= c; ++k) { free(sh[k]); sh[k] = NULL; }
            fclose(lf); return false;
        }
    }
    fclose(lf);
    return true;
}

bool client_static_volume_build(gi_static_volume_t *vol, const struct scene_desc *descp,
                                const char *base_dir, const lm_atlas_rect_t *mrect,
                                const lm_atlas_t *atlas, const float amin[3], const float amax[3])
{
    if (vol == NULL || descp == NULL || base_dir == NULL || atlas == NULL ||
        atlas->width == 0 || atlas->height == 0) return false;
    const scene_desc_t *desc = descp;
    if (desc->lightdata.lightmap_prefix[0] == '\0') return false;

    uint32_t aw = atlas->width, ah = atlas->height;
    char lm[512]; snprintf(lm, sizeof lm, "%s/%s", base_dir, desc->lightdata.lightmap_prefix);
    float *sh[4];
    if (!read_sh0_3(lm, aw, ah, sh)) return false;

    /* Coarse grid over the padded scene AABB (capped at 128^3). */
    float vox = getenv("GI_SVOX") ? (float)atof(getenv("GI_SVOX")) : 0.5f;
    if (vox < 0.05f) vox = 0.05f;
    float org[3]; int dims[3];
    for (int a = 0; a < 3; ++a) {
        org[a] = amin[a] - vox;
        int d = (int)((amax[a] - amin[a]) / vox) + 3;
        dims[a] = d < 1 ? 1 : (d > 128 ? 128 : d);
    }
    size_t cells = (size_t)dims[0] * dims[1] * dims[2];
    float *sum = calloc(cells * 3, sizeof(float));
    uint32_t *cnt = calloc(cells, sizeof(uint32_t));
    float *rgb = calloc(cells * 3, sizeof(float));
    if (!sum || !cnt || !rgb) { free(sum); free(cnt); free(rgb); for (int k=0;k<4;++k) free(sh[k]); return false; }

    for (uint32_t i = 0; i < desc->object_count; ++i) {
        const scene_desc_object_t *o = &desc->objects[i];
        char path[512]; snprintf(path, sizeof path, "%s/%s", base_dir, o->mesh);
        size_t sz = 0; FILE *f = fopen(path, "rb");
        if (f == NULL) continue;
        fseek(f, 0, SEEK_END); long fl = ftell(f); fseek(f, 0, SEEK_SET);
        uint8_t *bytes = (fl > 0) ? malloc((size_t)fl) : NULL;
        if (bytes == NULL || fread(bytes, 1, (size_t)fl, f) != (size_t)fl) { free(bytes); fclose(f); continue; }
        fclose(f); sz = (size_t)fl;
        mesh_slot_t slot; memset(&slot, 0, sizeof slot);
        bool ok = mesh_vao_deserialize(bytes, sz, &slot);
        free(bytes);
        if (!ok || slot.uvs[1] == NULL || slot.normals == NULL) { if (ok) mesh_slot_destroy(&slot); continue; }

        float mm[16]; memset(mm, 0, sizeof mm); mm[15] = 1.0f;
        svol_model(o->position, o->rotation, o->scale, mm);
        const lm_atlas_rect_t *rc = (mrect && mrect[i].w > 0) ? &mrect[i] : NULL;
        for (uint32_t v = 0; v < slot.vertex_count; ++v) {
            float u0 = slot.uvs[1][v*2], v0 = slot.uvs[1][v*2+1];
            if (u0 == 0.0f && v0 == 0.0f) continue;
            float au = u0, av = v0;
            if (rc) lm_atlas_remap_uv(rc, atlas, u0, v0, &au, &av);
            int px = (int)(au * (float)aw), py = (int)(av * (float)ah);
            if (px < 0) px = 0; else if (px >= (int)aw) px = (int)aw - 1;
            if (py < 0) py = 0; else if (py >= (int)ah) py = (int)ah - 1;
            size_t sp = ((size_t)py * aw + px) * 3;
            /* World normal (rotate + normalize). */
            const float *ln = &slot.normals[v*3];
            float nx = mm[0]*ln[0]+mm[4]*ln[1]+mm[8]*ln[2];
            float ny = mm[1]*ln[0]+mm[5]*ln[1]+mm[9]*ln[2];
            float nz = mm[2]*ln[0]+mm[6]*ln[1]+mm[10]*ln[2];
            float il = sqrtf(nx*nx+ny*ny+nz*nz); if (il > 1e-8f) { nx/=il; ny/=il; nz/=il; }
            float b0 = 0.282094792f, b1 = 0.488602512f*ny, b2 = 0.488602512f*nz, b3 = 0.488602512f*nx;
            float E[3];
            for (int c = 0; c < 3; ++c) {
                float e = 3.14159265f * sh[0][sp+c] * b0
                        + 2.09439510f * (sh[1][sp+c]*b1 + sh[2][sp+c]*b2 + sh[3][sp+c]*b3);
                E[c] = e > 0.0f ? e : 0.0f;
            }
            /* World position -> grid cell. */
            const float *lp = &slot.positions[v*3];
            float wx = mm[0]*lp[0]+mm[4]*lp[1]+mm[8]*lp[2]+mm[12];
            float wy = mm[1]*lp[0]+mm[5]*lp[1]+mm[9]*lp[2]+mm[13];
            float wz = mm[2]*lp[0]+mm[6]*lp[1]+mm[10]*lp[2]+mm[14];
            int cx = (int)((wx-org[0])/vox), cy = (int)((wy-org[1])/vox), cz = (int)((wz-org[2])/vox);
            if (cx<0||cy<0||cz<0||cx>=dims[0]||cy>=dims[1]||cz>=dims[2]) continue;
            size_t ci = ((size_t)cz*dims[1] + cy)*dims[0] + cx;
            sum[ci*3+0]+=E[0]; sum[ci*3+1]+=E[1]; sum[ci*3+2]+=E[2]; cnt[ci]++;
        }
        mesh_slot_destroy(&slot);
    }
    for (int k = 0; k < 4; ++k) free(sh[k]);

    for (size_t c = 0; c < cells; ++c)
        if (cnt[c]) { float inv = 1.0f/(float)cnt[c];
            rgb[c*3+0]=sum[c*3+0]*inv; rgb[c*3+1]=sum[c*3+1]*inv; rgb[c*3+2]=sum[c*3+2]*inv; }
    /* One dilation pass: fill empty cells from filled 6-neighbours. */
    for (int z = 0; z < dims[2]; ++z) for (int y = 0; y < dims[1]; ++y) for (int x = 0; x < dims[0]; ++x) {
        size_t ci = ((size_t)z*dims[1]+y)*dims[0]+x; if (cnt[ci]) continue;
        float acc[3] = {0,0,0}; int nn = 0;
        int off[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for (int oo = 0; oo < 6; ++oo) { int nx=x+off[oo][0], ny=y+off[oo][1], nz=z+off[oo][2];
            if (nx<0||ny<0||nz<0||nx>=dims[0]||ny>=dims[1]||nz>=dims[2]) continue;
            size_t nci = ((size_t)nz*dims[1]+ny)*dims[0]+nx; if (!cnt[nci]) continue;
            acc[0]+=rgb[nci*3+0]; acc[1]+=rgb[nci*3+1]; acc[2]+=rgb[nci*3+2]; ++nn; }
        if (nn) { rgb[ci*3+0]=acc[0]/nn; rgb[ci*3+1]=acc[1]/nn; rgb[ci*3+2]=acc[2]/nn; }
    }
    free(sum); free(cnt);

    bool ok = gi_static_volume_upload(vol, rgb, dims, org, vox);
    free(rgb);
    if (ok) fprintf(stderr, "[client] static-irr volume %dx%dx%d @ %.2fm\n", dims[0], dims[1], dims[2], (double)vox);
    return ok;
}

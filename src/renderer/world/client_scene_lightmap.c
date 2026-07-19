/**
 * @file client_scene_lightmap.c
 * @brief Load a baked SH lightmap atlas into 9 GL_TEXTURE_2D_ARRAY pages plus the
 *        per-mesh atlas rectangles needed to remap each mesh's uv1 (rpg-8302).
 *
 * Single-atlas (.flm) mode: one atlas layer. This is the one-chunk special case
 * of the general chunked lightmap system -- a level small enough to pack into a
 * single atlas. The FLM1 file stores, after the 9 SH coefficient images, one
 * lm_atlas_rect_t per mesh (in bake order); the caller remaps each mesh's local
 * uv1 into its rect (lm_atlas_remap_uv) so the shader can sample the shared atlas
 * with a plain uv1 lookup (there is no per-mesh rect uniform in the forward pass).
 */
#include <glad/glad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/renderer/client_scene.h"
#include "ferrum/lightmap/lm_atlas.h"

bool client_scene_load_lightmap(const gl_loader_t *loader, const char *lm_prefix,
                                uint32_t n_meshes, unsigned int sh_tex[9],
                                lm_atlas_rect_t *mrect, lm_atlas_t *atlas_out)
{
    (void)loader;
    for (int c = 0; c < 9; ++c) sh_tex[c] = 0;
    if (mrect != NULL)
        for (uint32_t i = 0; i < n_meshes; ++i) mrect[i] = (lm_atlas_rect_t){ 0, 0, 0, 0 };
    if (atlas_out != NULL) *atlas_out = (lm_atlas_t){ 0, 0 };
    if (lm_prefix == NULL || lm_prefix[0] == '\0') return false;

    FILE *lf = fopen(lm_prefix, "rb");
    if (lf == NULL) return false;

    char mg[4];
    uint32_t aw = 0, ah = 0, nc = 0, nmh = 0;
    if (fread(mg, 1, 4, lf) != 4 || memcmp(mg, "FLM1", 4) != 0 ||
        fread(&aw, 4, 1, lf) != 1 || fread(&ah, 4, 1, lf) != 1 ||
        fread(&nc, 4, 1, lf) != 1 || fread(&nmh, 4, 1, lf) != 1 ||
        aw == 0 || ah == 0) {
        fclose(lf);
        return false;
    }

    /* 9 single-layer RGB32F coeff arrays (layer 0 = the one atlas chunk). */
    for (int c = 0; c < 9; ++c) {
        glGenTextures(1, &sh_tex[c]);
        glBindTexture(GL_TEXTURE_2D_ARRAY, sh_tex[c]);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGB32F, (GLsizei)aw, (GLsizei)ah, 1,
                     0, GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    /* Upload the 9 coeff images (each aw*ah*3 floats) into layer 0. */
    size_t cpix = (size_t)aw * ah * 3u;
    float *buf = malloc(cpix * sizeof(float));
    bool ok = (buf != NULL);
    for (int c = 0; ok && c < 9; ++c) {
        if (fread(buf, sizeof(float), cpix, lf) != cpix) { ok = false; break; }
        glBindTexture(GL_TEXTURE_2D_ARRAY, sh_tex[c]);
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0,
                        (GLsizei)aw, (GLsizei)ah, 1, GL_RGB, GL_FLOAT, buf);
    }
    free(buf);

    /* Per-mesh atlas rectangles follow the coeff images (bake order == mesh
     * order). Read up to n_meshes; extras in the file are ignored. */
    if (ok && mrect != NULL) {
        for (uint32_t i = 0; i < nmh && i < n_meshes; ++i) {
            lm_atlas_rect_t r;
            if (fread(&r, sizeof r, 1, lf) != 1) break;
            mrect[i] = r;
        }
    }
    fclose(lf);

    if (!ok) {
        for (int c = 0; c < 9; ++c) {
            if (sh_tex[c]) { glDeleteTextures(1, &sh_tex[c]); sh_tex[c] = 0; }
        }
        return false;
    }
    if (atlas_out != NULL) { atlas_out->width = aw; atlas_out->height = ah; }
    return true;
}

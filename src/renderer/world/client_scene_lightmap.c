/**
 * @file client_scene_lightmap.c
 * @brief Load a baked SH lightmap into 9 GL_TEXTURE_2D_ARRAY pages (rpg-8302).
 *        Single-atlas (.flm) mode, lifted from hall_lit_dynamic.c's load_sh_arrays.
 */
#include <glad/glad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/renderer/client_scene.h"

bool client_scene_load_lightmap(const gl_loader_t *loader, const char *lm_prefix,
                                uint32_t n_meshes, unsigned int sh_tex[9],
                                int *sh_layer)
{
    (void)loader;
    for (uint32_t i = 0; i < n_meshes; ++i) sh_layer[i] = 0;
    for (int c = 0; c < 9; ++c) sh_tex[c] = 0;
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

    /* 9 single-layer RGB32F coeff arrays. */
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
    fclose(lf);
    if (!ok) {
        for (int c = 0; c < 9; ++c) {
            if (sh_tex[c]) { glDeleteTextures(1, &sh_tex[c]); sh_tex[c] = 0; }
        }
        return false;
    }
    return true;
}

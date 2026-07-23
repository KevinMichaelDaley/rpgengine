/**
 * @file lm_gpu_voxscene.c
 * @brief Per-run GPU residency of a bake mesh set (see
 *        lm_gpu_voxelize_internal.h, rpg-bpiz): upload every mesh overlapping
 *        a box (VBO/EBO/VAO) plus its deduped material textures; shared by
 *        the dense run, the point sampler and the GPU chunk-SVO build.
 */
#include "lm_gpu_voxelize_internal.h"

#include <stdlib.h>

bool lm_voxi_scene_upload(const lm_mesh_t *meshes, uint32_t n_meshes,
                          const float bmin[3], const float bmax[3],
                          lm_voxi_scene_t *out)
{
    lm_voxi_gl_t *gl = &lm_voxi_gl;
    (void)gl;
    out->gm = calloc(n_meshes ? n_meshes : 1u, sizeof *out->gm);
    out->imgs = calloc((size_t)n_meshes * 2u + 1u, sizeof *out->imgs);
    out->img_tex = calloc((size_t)n_meshes * 2u + 1u, sizeof *out->img_tex);
    out->n_gm = out->n_img = 0;
    if (out->gm == NULL || out->imgs == NULL || out->img_tex == NULL) {
        lm_voxi_scene_free(out);
        return false;
    }
    for (uint32_t i = 0; i < n_meshes; ++i) {
        const lm_mesh_t *m = &meshes[i];
        if (m->index_count < 3 || m->positions == NULL || m->indices == NULL)
            continue;
        if (!lm_voxi_mesh_overlaps(m, bmin, bmax)) continue;
        if (!lm_voxi_upload_mesh(m, &out->gm[out->n_gm])) {
            lm_voxi_scene_free(out);
            return false;
        }
        const lm_image_t *want[2] = { m->albedo_image, m->emissive_image };
        GLuint got[2] = { 0u, 0u };
        for (int k = 0; k < 2; ++k) {
            if (want[k] == NULL) continue;
            for (uint32_t j = 0; j < out->n_img; ++j)
                if (out->imgs[j] == want[k]) { got[k] = out->img_tex[j]; break; }
            if (got[k] == 0u) {
                got[k] = lm_voxi_upload_image(want[k]);
                if (got[k] != 0u) {
                    out->imgs[out->n_img] = want[k];
                    out->img_tex[out->n_img] = got[k];
                    ++out->n_img;
                }
            }
        }
        out->gm[out->n_gm].alb_tex = got[0];
        out->gm[out->n_gm].emi_tex = got[1];
        ++out->n_gm;
    }
    return true;
}

void lm_voxi_scene_free(lm_voxi_scene_t *s)
{
    lm_voxi_gl_t *gl = &lm_voxi_gl;
    if (s->gm != NULL)
        for (uint32_t i = 0; i < s->n_gm; ++i) lm_voxi_free_mesh(&s->gm[i]);
    if (s->img_tex != NULL)
        for (uint32_t j = 0; j < s->n_img; ++j)
            if (s->img_tex[j]) gl->DeleteTextures(1, &s->img_tex[j]);
    free(s->gm);
    free((void *)s->imgs);
    free(s->img_tex);
    s->gm = NULL;
    s->imgs = NULL;
    s->img_tex = NULL;
    s->n_gm = s->n_img = 0;
}

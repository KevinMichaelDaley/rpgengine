/**
 * @file refl_bake_chunks.c
 * @brief Bake-mode per-chunk reflection-probe bake (see refl_bake.h,
 *        rpg-wlh9): dense grids per SDF chunk over the lm mesh set,
 *        streamed at runtime with the same visibility-gated chunk
 *        residency as the .sdf/.probesh sidecars.
 */
#include "ferrum/renderer/gi/refl_bake.h"

#include <stdio.h>
#include <string.h>

#include "ferrum/lightmap/lm_light.h"
#include "ferrum/lightmap/lm_mesh.h"
#include "ferrum/probe/place/probe_chunk_sdf.h"
#include "ferrum/renderer/gi/refl_bake_lm.h"

/* Face renderer for the bake-mode path: clear + minimal lit draw of the
 * uploaded lm set. Matches the refl_bake_params_t.render_fn contract. */
struct refl_lm_ctx {
    refl_bake_t *rb;
    refl_lm_set_t *set;
    const lm_mesh_t *meshes;
    float sun_dir[3];
    float sun_color[3];
    float ambient[3];
    float sun_vis;
};

static void lm_face_render(void *user, uint32_t fbo, const float view[16],
                           const float proj[16], const float eye[3],
                           uint32_t face_res, float sun_vis,
                           const float ambient[3])
{
    (void)fbo;
    (void)eye;
    (void)face_res;
    struct refl_lm_ctx *c = (struct refl_lm_ctx *)user;
    c->sun_vis = sun_vis;
    refl_bake_t *rb = c->rb;
    /* vp = proj * view (column-major). */
    float vp[16];
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row) {
            float acc = 0.0f;
            for (int k = 0; k < 4; ++k)
                acc += proj[k * 4 + row] * view[col * 4 + k];
            vp[col * 4 + row] = acc;
        }
    rb->glEnable(GL_DEPTH_TEST);
    rb->glDepthFunc(GL_LESS);
    shader_program_bind(&rb->shader);
    shader_uniform_set_mat4(&rb->cache, &rb->shader, "u_vp", vp, 0);
    shader_uniform_set_vec3(&rb->cache, &rb->shader, "u_sun_dir",
                            c->sun_dir);
    shader_uniform_set_vec3(&rb->cache, &rb->shader, "u_sun_color",
                            c->sun_color);
    shader_uniform_set_vec3(&rb->cache, &rb->shader, "u_ambient",
                            ambient != NULL ? ambient : c->ambient);
    shader_uniform_set_float(&rb->cache, &rb->shader, "u_sun_vis",
                             c->sun_vis);
    refl_lm_draw(c->set, rb, c->meshes);
}

bool refl_bake_chunks(const gl_loader_t *loader,
                      const struct lm_mesh *meshes, uint32_t n_meshes,
                      const struct lm_light *sun, const float sky[3],
                      const char *sdf_prefix,
                      const refl_bake_params_t *prm_in)
{
    if (loader == NULL || meshes == NULL || n_meshes == 0u ||
        sdf_prefix == NULL || prm_in == NULL)
        return false;
    probe_chunk_sdf_t cs;
    if (!probe_chunk_sdf_open(sdf_prefix, &cs)) {
        fprintf(stderr, "refl_bake: no SDF chunks at '%s' -- skipping\n",
                sdf_prefix);
        return false;
    }

    refl_bake_params_t prm = *prm_in;
    if (prm.spacing <= 0.0f)
        prm.spacing = 2.5f;      /* DENSE: room scale. */
    if (prm.max_probes == 0u)
        prm.max_probes = 128u;   /* per chunk. */

    /* One shared GL renderer + mesh upload for every chunk. */
    refl_bake_t rb;
    memset(&rb, 0, sizeof rb);
    uint32_t face_res = (prm.face_res >= 8u)
                            ? prm.face_res
                            : ((prm.tile_res >= 8u ? prm.tile_res : 64u) *
                               2u);
    refl_lm_set_t set;
    bool ok = refl_bake_init(&rb, loader, face_res) &&
              refl_lm_upload(&set, loader, meshes, n_meshes);

    struct refl_lm_ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.rb = &rb;
    ctx.set = &set;
    ctx.meshes = meshes;
    if (sun != NULL) {
        /* lm sun direction points AWAY from the light; the shader wants
         * the direction TOWARD it. */
        ctx.sun_dir[0] = -sun->direction.x;
        ctx.sun_dir[1] = -sun->direction.y;
        ctx.sun_dir[2] = -sun->direction.z;
        ctx.sun_color[0] = sun->color.x;
        ctx.sun_color[1] = sun->color.y;
        ctx.sun_color[2] = sun->color.z;
    }
    for (int a = 0; a < 3; ++a) {
        ctx.ambient[a] = (sky != NULL) ? sky[a] : 0.2f;
        prm.sky[a] = (sky != NULL) ? sky[a] : 0.05f;
    }
    prm.render_fn = lm_face_render;
    prm.render_user = &ctx;
    /* Per-probe sun visibility comes from the orchestrator's cone march;
     * the minimal path applies it via refl_bake_probe -- for the callback
     * path feed it through the ctx (refl_bake_run recomputes per probe,
     * and lm_face_render reads the latest value). */
    prm.sun_dir[0] = ctx.sun_dir[0];
    prm.sun_dir[1] = ctx.sun_dir[1];
    prm.sun_dir[2] = ctx.sun_dir[2];
    prm.sun_color[0] = ctx.sun_color[0];
    prm.sun_color[1] = ctx.sun_color[1];
    prm.sun_color[2] = ctx.sun_color[2];
    prm.ambient[0] = ctx.ambient[0];
    prm.ambient[1] = ctx.ambient[1];
    prm.ambient[2] = ctx.ambient[2];

    uint32_t baked_chunks = 0u;
    for (uint32_t c = 0; ok && c < cs.count; ++c) {
        const lm_sdf_data_t *ch = &cs.chunks[c];
        float mn[3], mx[3];
        for (int a = 0; a < 3; ++a) {
            mn[a] = ch->origin[a];
            mx[a] = ch->origin[a] + (float)ch->dims[a] * ch->voxel;
        }
        char out[576];
        snprintf(out, sizeof out, "%s_c%03u.rprobe", sdf_prefix,
                 (unsigned)cs.file_no[c]);
        prm.place_min = mn;
        prm.place_max = mx;
        prm.out_path = out;
        prm.sdf_fn = probe_chunk_sdf_sample;
        prm.sdf_user = &cs;
        /* sun_vis for the whole chunk's probes is refined per probe by
         * refl_bake_run; ctx.sun_vis defaults open. */
        ctx.sun_vis = 1.0f;
        if (refl_bake_run(loader, NULL, sdf_prefix, &prm))
            baked_chunks += 1u;
    }
    uint32_t n_chunks = cs.count;
    refl_lm_destroy(&set);
    refl_bake_destroy(&rb);
    probe_chunk_sdf_close(&cs);
    fprintf(stderr, "refl_bake: %u/%u chunks wrote .rprobe grids\n",
            baked_chunks, n_chunks);
    return baked_chunks > 0u;
}

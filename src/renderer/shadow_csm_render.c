/**
 * @file shadow_csm_render.c
 * @brief Static EVSM2 cascade bake (cached) + single dynamic distance map
 *        (per-frame) rendering (see shadow_csm.h).
 */
#include "ferrum/renderer/shadow_csm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ferrum/renderer/gl_constants.h"
#include "ferrum/renderer/mesh/static_mesh.h"

/* Far-plane EVSM2 moments (d=1): w=exp(C), w^2=exp(2C). An empty texel must read
 * as the most distant occluder. These exceed [0,1], so they are cleared with the
 * unclamped glClearBufferfv (glClearColor would clamp to 1). C matches SC_FS. */
#define CSM_EVSM_C 30.0f

/* Extract the 6 frustum planes (Gribb-Hartmann) from a column-major light MVP,
 * each normalised so plane.dot(p) is a signed distance. */
static void csm_extract_planes(const float m[16], float pl[6][4])
{
    /* rows of the matrix: row_r = (m[r], m[4+r], m[8+r], m[12+r]). */
    for (int i = 0; i < 6; ++i) {
        int r = i >> 1;             /* 0=x,1=y,2=z plane pair */
        float s = (i & 1) ? -1.0f : 1.0f;  /* + = left/bottom/near, - = right/top/far */
        for (int k = 0; k < 4; ++k)
            pl[i][k] = m[k*4 + 3] + s * m[k*4 + r];
        float n = sqrtf(pl[i][0]*pl[i][0] + pl[i][1]*pl[i][1] + pl[i][2]*pl[i][2]);
        if (n > 1e-8f) for (int k = 0; k < 4; ++k) pl[i][k] /= n;
    }
}

/* True if the mesh's world AABB (from its local AABB * model) is fully outside
 * any cascade plane -> can be skipped for this cascade's shadow pass. Uses the
 * exact AABB-vs-plane "positive vertex" test (tight, no sphere approximation). */
static int csm_cull(const float pl[6][4], const float model[16],
                    const float lmin[3], const float lmax[3])
{
    float wmin[3] = { 1e30f,1e30f,1e30f }, wmax[3] = { -1e30f,-1e30f,-1e30f };
    for (int c = 0; c < 8; ++c) {
        float lc[3] = { (c&1)?lmax[0]:lmin[0], (c&2)?lmax[1]:lmin[1], (c&4)?lmax[2]:lmin[2] };
        for (int r = 0; r < 3; ++r) {
            float w = model[0*4+r]*lc[0] + model[1*4+r]*lc[1] + model[2*4+r]*lc[2] + model[3*4+r];
            if (w < wmin[r]) wmin[r] = w;
            if (w > wmax[r]) wmax[r] = w;
        }
    }
    for (int i = 0; i < 6; ++i) {
        /* positive vertex: the AABB corner farthest along this plane's normal. */
        float px = pl[i][0] >= 0.0f ? wmax[0] : wmin[0];
        float py = pl[i][1] >= 0.0f ? wmax[1] : wmin[1];
        float pz = pl[i][2] >= 0.0f ? wmax[2] : wmin[2];
        if (pl[i][0]*px + pl[i][1]*py + pl[i][2]*pz + pl[i][3] < 0.0f)
            return 1; /* even the nearest corner is behind the plane -> outside */
    }
    return 0;
}

/* Draw scene items [from, to) into the cascade whose light MVP is @p vp. When
 * @p cascade_filter >= 0 only casters CLASSIFIED into that cascade are drawn (so
 * each static cascade holds just its size tier); -1 draws all (the single
 * dynamic map). The light-frustum box cull is a secondary guard. */
static void csm_draw_items(shadow_csm_t *csm, const render_scene_t *scene,
                           uint32_t from, uint32_t to, const float vp[16],
                           int cascade_filter)
{
    float planes[6][4];
    csm_extract_planes(vp, planes);
    uint32_t drawn = 0, total = 0;
    for (uint32_t i = from; i < to; ++i) {
        const render_renderable_t *r = &scene->items[i];
        if (r->mesh == NULL)
            continue;
        ++total;
        /* Size/background classification: a caster belongs to exactly one static
         * cascade. (Dynamic pass passes -1 -> no filter.) */
        if (cascade_filter >= 0 &&
            shadow_csm_cascade_of(csm, r) != (uint32_t)cascade_filter)
            continue;
        if (csm_cull(planes, r->model, r->mesh->aabb_min, r->mesh->aabb_max))
            continue;
        ++drawn;
        shader_uniform_set_mat4(&csm->cache, &csm->shader, "u_model", r->model, 0);
        static_mesh_bind(r->mesh);
        for (uint32_t s = 0; s < r->mesh->submesh_count; ++s)
            static_mesh_draw_submesh(r->mesh, s);
    }
    if (getenv("CSM_DEBUG"))
        fprintf(stderr, "  cascade draw: %u/%u meshes\n", drawn, total);
}

/* Common depth-pass draw state (no FBO bind -- the caller attaches its target).
 * Both sides are rendered (cull disabled) because the room's single-sided
 * surfaces would otherwise drop their sun-facing back faces and leak light.
 * u_view = identity; the light matrix is fed as u_projection. */
static void csm_begin_pass(shadow_csm_t *csm, uint32_t res, int mode)
{
    mat4_t identity = mat4_identity();
    csm->glViewport(0, 0, (int32_t)res, (int32_t)res);
    csm->glDisable(GL_CULL_FACE);
    csm->glEnable(GL_DEPTH_TEST);
    csm->glDepthFunc(GL_LESS);
    shader_program_bind(&csm->shader);
    shader_uniform_set_mat4(&csm->cache, &csm->shader, "u_view", identity.m, 0);
    shader_uniform_set_int(&csm->cache, &csm->shader, "u_mode", mode);
}

void shadow_csm_bake_static(shadow_csm_t *csm, const render_scene_t *scene)
{
    if (csm == NULL || scene == NULL || csm->static_valid || csm->static_base < 0)
        return;
    uint32_t to = scene->dynamic_from;
    if (to > scene->count)
        to = scene->count;

    float fw = expf(CSM_EVSM_C);
    float far_moments[2] = { fw, fw * fw };
    csm_begin_pass(csm, csm->static_res, 0 /* EVSM */);
    for (uint32_t c = 0; c < csm->cascades; ++c) {
        /* Attach this cascade's slice of the resource-managed shadow atlas. */
        shadow_atlas_bind_layer(&csm->static_atlas,
                                (uint32_t)csm->static_base + c);
        csm->glClearBufferfv(GL_COLOR, 0, far_moments); /* far state (unclamped). */
        csm->glClear(GL_DEPTH_BUFFER_BIT);
        shader_uniform_set_mat4(&csm->cache, &csm->shader, "u_projection",
                                csm->view_proj[c].m, 0);
        shader_uniform_set_vec3(&csm->cache, &csm->shader, "u_eye", csm->eye[c]);
        shader_uniform_set_float(&csm->cache, &csm->shader, "u_far", csm->far_plane[c]);
        csm_draw_items(csm, scene, 0u, to, csm->view_proj[c].m, (int)c);

        /* CSM_DUMP: read this cascade's EVSM2 map back and write its DEPTH
         * (decoded from the first moment, d = ln(m)/C) as a grayscale PPM, so the
         * caster depth per cascade can be inspected directly. */
        if (getenv("CSM_DUMP")) {
            uint32_t res = csm->static_res;
            float *buf = malloc((size_t)res * res * 2 * sizeof(float));
            if (buf) {
                csm->glReadPixels(0, 0, (int32_t)res, (int32_t)res, GL_RG, GL_FLOAT, buf);
                char path[64]; snprintf(path, sizeof path, "csm_cascade_%u.pgm", c);
                FILE *fp = fopen(path, "wb");
                if (fp) {
                    fprintf(fp, "P5\n%u %u\n255\n", res, res);
                    /* glReadPixels rows are bottom-to-top; write top row first. */
                    for (int y = (int)res - 1; y >= 0; --y)
                    for (uint32_t x = 0; x < res; ++x) {
                        float m = buf[((uint32_t)y * res + x) * 2];
                        float d = m > 1.0f ? logf(m) / CSM_EVSM_C : 0.0f;
                        int v = (int)(d * 255.0f); v = v < 0 ? 0 : (v > 255 ? 255 : v);
                        unsigned char b = (unsigned char)v; fwrite(&b, 1, 1, fp);
                    }
                    fclose(fp);
                    fprintf(stderr, "CSM_DUMP: wrote %s (%ux%u)\n", path, res, res);
                }
                free(buf);
            }
        }
    }
    csm->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    csm->static_valid = true;
}

void shadow_csm_render_dynamic(shadow_csm_t *csm, const render_scene_t *scene)
{
    if (csm == NULL || scene == NULL)
        return;
    uint32_t from = scene->dynamic_from;
    if (from > scene->count)
        from = scene->count;

    /* One orthographic distance face; plain depth for PCF at receive time.
     * Cleared to 1 (= far / no occluder) so an empty dynamic set leaves only the
     * static term after the co-sample. */
    csm->glBindFramebuffer(GL_FRAMEBUFFER, csm->fbo);
    csm->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_TEXTURE_2D, csm->dyn_map, 0);
    csm->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                   GL_RENDERBUFFER, csm->dyn_depth_rb);
    csm_begin_pass(csm, csm->dynamic_res, 1 /* distance */);
    csm->glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    csm->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    shader_uniform_set_mat4(&csm->cache, &csm->shader, "u_projection",
                            csm->dyn_view_proj.m, 0);
    shader_uniform_set_vec3(&csm->cache, &csm->shader, "u_eye", csm->dyn_eye);
    shader_uniform_set_float(&csm->cache, &csm->shader, "u_far", csm->dyn_far);
    csm_draw_items(csm, scene, from, scene->count, csm->dyn_view_proj.m, -1);
    csm->glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/**
 * @file shadow_caustics_bake.c
 * @brief Caustics bake (clear -> trace -> resolve per cascade) and receiver
 *        binding (see shadow_caustics.h).
 */
#include "ferrum/renderer/shadow_caustics.h"

#include <glad/glad.h>
#include <string.h>

#include "ferrum/math/mat4.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"

/* 4.2+ enums glad (3.3) lacks. */
#ifndef GL_READ_WRITE
#define GL_READ_WRITE 0x88BA
#endif
#ifndef GL_R32UI
#define GL_R32UI 0x8236
#endif
#ifndef GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT 0x00000020
#endif
#ifndef GL_TEXTURE_FETCH_BARRIER_BIT
#define GL_TEXTURE_FETCH_BARRIER_BIT 0x00000008
#endif
#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif

/* Dispatch the resolve program over the map in the given mode (0 = clear the
 * cascade's accumulator layers, 1 = accumulator -> RGBA16F map). */
static void resolve_pass(shadow_caustics_t *c, uint32_t cascade, int mode)
{
    glUseProgram(c->prog_resolve);
    glUniform1i(c->loc.rz_mode, mode);
    glUniform1i(c->loc.rz_cascade, (GLint)cascade);
    glUniform1i(c->loc.rz_res, (GLint)c->resolution);
    uint32_t groups = (c->resolution + 7u) / 8u;
    c->DispatchCompute(groups, groups, 1u);
    c->MemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                     GL_TEXTURE_FETCH_BARRIER_BIT);
}

void shadow_caustics_bake(shadow_caustics_t *c, uint32_t mask_color_tex,
                          uint32_t mask_depth_tex, uint32_t mask_layer,
                          uint32_t cascade, const float vp[16],
                          const float eye[3], float far_plane)
{
    if (c == NULL || c->prog_trace == 0u || cascade >= c->cascades ||
        vp == NULL || eye == NULL || far_plane <= 0.0f)
        return;

    mat4_t m, inv;
    memcpy(m.m, vp, sizeof m.m);
    if (mat4_inverse(m, &inv) != 0)
        return;                       /* singular light matrix: nothing to do. */

    /* Both passes share the image bindings: accumulator on image unit 0
     * (layered), the resolved map on 1. */
    c->BindImageTexture(0u, c->accum_tex, 0, 1 /* layered */, 0,
                        GL_READ_WRITE, GL_R32UI);
    c->BindImageTexture(1u, c->map_tex, 0, 1 /* layered */, 0,
                        GL_READ_WRITE, GL_RGBA16F);

    resolve_pass(c, cascade, 0);      /* clear this cascade's accumulator. */

    glUseProgram(c->prog_trace);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, mask_color_tex);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D_ARRAY, mask_depth_tex);
    glUniform1i(glGetUniformLocation(c->prog_trace, "u_mask_color"), 0);
    glUniform1i(glGetUniformLocation(c->prog_trace, "u_mask_depth"), 1);
    glUniformMatrix4fv(c->loc.vp, 1, GL_FALSE, vp);
    glUniformMatrix4fv(c->loc.inv_vp, 1, GL_FALSE, inv.m);
    glUniform3fv(c->loc.eye, 1, eye);
    glUniform1f(c->loc.far, far_plane);
    glUniform1i(c->loc.res, (GLint)c->resolution);
    glUniform1i(c->loc.samples, (GLint)c->samples);
    glUniform1f(c->loc.scatter, c->scatter);
    glUniform1f(c->loc.scatter_dist, c->scatter_dist);
    glUniform1f(c->loc.max_dist, c->max_dist);
    glUniform1i(c->loc.cascade, (GLint)cascade);
    glUniform1i(c->loc.seed, (GLint)(cascade * 977u + 1u));
    glUniform1i(c->loc.mask_layer, (GLint)mask_layer);
    glUniform1i(c->loc.sdf_count, (GLint)c->sdf_count);
    /* EVERY u_sdf[i] gets its own unit even beyond sdf_count: an unassigned
     * sampler3D defaults to unit 0 where the mask sampler2DArray lives -- a
     * type conflict that silently invalidates the whole dispatch. */
    for (uint32_t i = 0; i < SHADOW_CAUSTICS_MAX_SDF; ++i) {
        glUniform1i(c->loc.sdf[i], (GLint)(2u + i));
        glActiveTexture(GL_TEXTURE0 + 2 + i);
        glBindTexture(GL_TEXTURE_3D, i < c->sdf_count ? c->sdf_tex[i] : 0u);
        if (i < c->sdf_count) {
            glUniform3fv(c->loc.sdf_origin[i], 1, c->sdf_origin[i]);
            glUniform3fv(c->loc.sdf_dim[i], 1, c->sdf_dim[i]);
            glUniform1f(c->loc.sdf_vox[i], c->sdf_vox[i]);
        }
    }
    /* Global zone fallback on unit 18 (2 + MAX_SDF); assigned even when off
     * so the sampler3D never aliases unit 0's sampler2DArray. */
    glUniform1i(c->loc.zone, 2 + SHADOW_CAUSTICS_MAX_SDF);
    glUniform1i(c->loc.zone_on, c->zone_tex != 0u ? 1 : 0);
    glActiveTexture(GL_TEXTURE0 + 2 + SHADOW_CAUSTICS_MAX_SDF);
    glBindTexture(GL_TEXTURE_3D, c->zone_tex);
    if (c->zone_tex != 0u) {
        glUniform3fv(c->loc.zone_origin, 1, c->zone_origin);
        glUniform3fv(c->loc.zone_dim, 1, c->zone_dim);
        glUniform1f(c->loc.zone_vox, c->zone_vox);
    }
    glActiveTexture(GL_TEXTURE0);
    uint32_t groups = (c->resolution + 7u) / 8u;
    c->DispatchCompute(groups, groups, 1u);
    c->MemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    resolve_pass(c, cascade, 1);      /* fixed point -> filterable map. */
    glUseProgram(0);
}

void shadow_caustics_bind(const shadow_caustics_t *c,
                          struct shader_uniform_cache *cache,
                          const struct shader_program *program, uint32_t unit)
{
    /* Always assign the sampler its unit (declared samplers must never share
     * unit 0 with a different type), even when caustics are unavailable. */
    shader_uniform_set_int(cache, program, "u_csm_caustic", (int32_t)unit);
    int on = (c != NULL && c->map_tex != 0u) ? 1 : 0;
    shader_uniform_set_int(cache, program, "u_caustic_on", on);
    if (!on)
        return;
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, c->map_tex);
    glActiveTexture(GL_TEXTURE0);
}

uint32_t shadow_caustics_map_texture(const shadow_caustics_t *c)
{
    return c != NULL ? c->map_tex : 0u;
}

/**
 * @file shadow_csm_bind.c
 * @brief Bind the cascade arrays + set the u_csm_* uniforms (see shadow_csm.h).
 */
#include "ferrum/renderer/shadow_csm.h"

#include "ferrum/renderer/gl_constants.h"

/* The uniform cache keys entries by the name POINTER it is first given (it does
 * not copy the string), so every array-element name must be a stable, distinct
 * literal -- not a reused local buffer. One row per cascade slot. */
static const char *const CSM_VP[SHADOW_CSM_MAX_CASCADES] = {
    "u_csm_vp[0]", "u_csm_vp[1]", "u_csm_vp[2]", "u_csm_vp[3]",
    "u_csm_vp[4]", "u_csm_vp[5]", "u_csm_vp[6]", "u_csm_vp[7]" };
static const char *const CSM_EYE[SHADOW_CSM_MAX_CASCADES] = {
    "u_csm_eye[0]", "u_csm_eye[1]", "u_csm_eye[2]", "u_csm_eye[3]",
    "u_csm_eye[4]", "u_csm_eye[5]", "u_csm_eye[6]", "u_csm_eye[7]" };
static const char *const CSM_FAR[SHADOW_CSM_MAX_CASCADES] = {
    "u_csm_far[0]", "u_csm_far[1]", "u_csm_far[2]", "u_csm_far[3]",
    "u_csm_far[4]", "u_csm_far[5]", "u_csm_far[6]", "u_csm_far[7]" };
static const char *const CSM_SPLIT[SHADOW_CSM_MAX_CASCADES] = {
    "u_csm_split[0]", "u_csm_split[1]", "u_csm_split[2]", "u_csm_split[3]",
    "u_csm_split[4]", "u_csm_split[5]", "u_csm_split[6]", "u_csm_split[7]" };

void shadow_csm_bind(const shadow_csm_t *csm, shader_uniform_cache_t *cache,
                     const shader_program_t *program, uint32_t unit_static,
                     uint32_t unit_dynamic)
{
    if (csm == NULL || cache == NULL || program == NULL)
        return;
    /* Static EVSM2 cascades from the resource-managed atlas on unit_static; the
     * single dynamic distance map on unit_dynamic (co-sampled + PCF by receiver).
     * The atlas is dedicated to this light so static_base == 0 and cascade layer
     * ci in the shader maps directly to atlas layer ci. */
    shadow_atlas_bind_sample(&csm->static_atlas, unit_static);
    csm->glActiveTexture(GL_TEXTURE0 + unit_dynamic);
    csm->glBindTexture(GL_TEXTURE_2D, csm->dyn_map);

    shader_uniform_set_int(cache, program, "u_csm_static", (int32_t)unit_static);
    shader_uniform_set_int(cache, program, "u_dyn_map", (int32_t)unit_dynamic);
    shader_uniform_set_int(cache, program, "u_csm_count", (int32_t)csm->cascades);
    shader_uniform_set_int(cache, program, "u_csm_enabled", 1);
    shader_uniform_set_mat4(cache, program, "u_dyn_vp", csm->dyn_view_proj.m, 0);
    shader_uniform_set_vec3(cache, program, "u_dyn_eye", csm->dyn_eye);
    shader_uniform_set_float(cache, program, "u_dyn_far", csm->dyn_far);

    for (uint32_t c = 0; c < csm->cascades; ++c) {
        shader_uniform_set_mat4(cache, program, CSM_VP[c], csm->view_proj[c].m, 0);
        shader_uniform_set_vec3(cache, program, CSM_EYE[c], csm->eye[c]);
        shader_uniform_set_float(cache, program, CSM_FAR[c], csm->far_plane[c]);
        shader_uniform_set_float(cache, program, CSM_SPLIT[c], csm->split_view[c]);
    }
}

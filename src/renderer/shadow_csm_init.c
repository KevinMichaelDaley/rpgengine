/**
 * @file shadow_csm_init.c
 * @brief Cascaded shadow-map resource creation + teardown (see shadow_csm.h).
 */
#include "ferrum/renderer/shadow_csm.h"

#include <string.h>

#include "ferrum/renderer/gl_constants.h"

/* World-position pass-through VS; the FS writes the fragment's linear distance
 * from the cascade's virtual eye, normalised by its far plane. Identical shape
 * to the point/spot/dir maps so the compare in pbr_shader is consistent. */
static const char *const SC_VS =
    "#version 330 core\n"
    "layout(location=0) in vec3 in_position;\n"
    "uniform mat4 u_model;\n"
    "uniform mat4 u_view;\n"
    "uniform mat4 u_projection;\n"
    "out vec3 v_world;\n"
    "void main(){\n"
    "  vec4 wp = u_model * vec4(in_position, 1.0);\n"
    "  v_world = wp.xyz;\n"
    "  gl_Position = u_projection * u_view * wp;\n"
    "}\n";
/* Plain linear-distance depth map (R32F): stores normalised distance from the
 * cascade eye. Both the static cascades and the dynamic map store the same thing;
 * the receiver runs PCSS (blocker search + variable PCF) over it. */
static const char *const SC_FS =
    "#version 330 core\n"
    "in vec3 v_world;\n"
    "uniform vec3 u_eye;\n"
    "uniform float u_far;\n"
    "uniform int u_mode;\n" /* unused now; both modes store linear distance. */
    "layout(location=0) out float o_depth;\n"
    "void main(){ o_depth = clamp(distance(v_world, u_eye) / u_far, 0.0, 1.0); }\n";

#define SC_LOAD(dst, name)                                                    \
    do {                                                                      \
        void *p_ = loader->get_proc_address((name), loader->user_data);       \
        if (p_ == NULL) return false;                                         \
        memcpy(&(dst), &p_, sizeof(p_));                                      \
    } while (0)

/* Allocate the single dynamic-caster distance map: R32F 2D, linear-filtered for
 * PCF interpolation, with its own depth renderbuffer. */
static void sc_make_dyn(shadow_csm_t *csm, uint32_t res)
{
    csm->glGenTextures(1, &csm->dyn_map);
    csm->glBindTexture(GL_TEXTURE_2D, csm->dyn_map);
    csm->glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, (int32_t)res, (int32_t)res, 0,
                      GL_RED, GL_FLOAT, NULL);
    csm->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    csm->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    csm->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    csm->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    csm->glGenRenderbuffers(1, &csm->dyn_depth_rb);
    csm->glBindRenderbuffer(GL_RENDERBUFFER, csm->dyn_depth_rb);
    csm->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                               (int32_t)res, (int32_t)res);
}

bool shadow_csm_init(shadow_csm_t *csm, const shadow_csm_config_t *config)
{
    if (csm == NULL || config == NULL || config->loader == NULL ||
        config->loader->get_proc_address == NULL || config->cascades == 0u ||
        config->cascades > SHADOW_CSM_MAX_CASCADES ||
        config->static_res == 0u || config->dynamic_res == 0u)
        return false;
    const gl_loader_t *loader = config->loader;
    memset(csm, 0, sizeof(*csm));
    csm->cascades = config->cascades;
    csm->static_res = config->static_res;
    csm->dynamic_res = config->dynamic_res;
    csm->lambda = config->lambda;
    csm->max_distance = config->max_distance;
    csm->softness = config->softness;
    csm->pcss = config->pcss;
    /* Static-bake MSAA: clamp to the sample counts every GL3.3 part supports.
     * 0/1 = single-sampled (no scratch targets, no resolve blit). */
    csm->msaa = config->msaa;
    if (csm->msaa <= 1u)
        csm->msaa = 0u;
    else if (csm->msaa < 4u)
        csm->msaa = 2u;
    else if (csm->msaa < 8u)
        csm->msaa = 4u;
    else
        csm->msaa = 8u;
    csm->mask_res = config->mask_res ? config->mask_res : config->static_res;

    if (shader_program_create(&csm->shader, loader, SC_VS, SC_FS, NULL, 0) !=
        SHADER_PROGRAM_OK)
        return false;
    shader_uniform_cache_init(&csm->cache, &csm->shader);

    SC_LOAD(csm->glGenFramebuffers, "glGenFramebuffers");
    SC_LOAD(csm->glDeleteFramebuffers, "glDeleteFramebuffers");
    SC_LOAD(csm->glBindFramebuffer, "glBindFramebuffer");
    SC_LOAD(csm->glFramebufferTextureLayer, "glFramebufferTextureLayer");
    SC_LOAD(csm->glGenRenderbuffers, "glGenRenderbuffers");
    SC_LOAD(csm->glDeleteRenderbuffers, "glDeleteRenderbuffers");
    SC_LOAD(csm->glBindRenderbuffer, "glBindRenderbuffer");
    SC_LOAD(csm->glRenderbufferStorage, "glRenderbufferStorage");
    SC_LOAD(csm->glRenderbufferStorageMultisample, "glRenderbufferStorageMultisample");
    SC_LOAD(csm->glFramebufferRenderbuffer, "glFramebufferRenderbuffer");
    SC_LOAD(csm->glBlitFramebuffer, "glBlitFramebuffer");
    SC_LOAD(csm->glReadBuffer, "glReadBuffer");
    SC_LOAD(csm->glGenTextures, "glGenTextures");
    SC_LOAD(csm->glDeleteTextures, "glDeleteTextures");
    SC_LOAD(csm->glBindTexture, "glBindTexture");
    SC_LOAD(csm->glActiveTexture, "glActiveTexture");
    SC_LOAD(csm->glGenerateMipmap, "glGenerateMipmap");
    SC_LOAD(csm->glGetTexImage, "glGetTexImage");
    SC_LOAD(csm->glTexSubImage3D, "glTexSubImage3D");
    SC_LOAD(csm->glFramebufferTexture2D, "glFramebufferTexture2D");
    SC_LOAD(csm->glTexImage2D, "glTexImage2D");
    SC_LOAD(csm->glTexImage3D, "glTexImage3D");
    SC_LOAD(csm->glTexParameteri, "glTexParameteri");
    SC_LOAD(csm->glViewport, "glViewport");
    SC_LOAD(csm->glClearColor, "glClearColor");
    SC_LOAD(csm->glClearBufferfv, "glClearBufferfv");
    SC_LOAD(csm->glClear, "glClear");
    SC_LOAD(csm->glEnable, "glEnable");
    SC_LOAD(csm->glDisable, "glDisable");
    SC_LOAD(csm->glDepthFunc, "glDepthFunc");
    SC_LOAD(csm->glGetError, "glGetError");
    SC_LOAD(csm->glCheckFramebufferStatus, "glCheckFramebufferStatus");
    SC_LOAD(csm->glReadPixels, "glReadPixels");

    /* Static cascades live in a resource-managed EVSM2 shadow atlas: the array
     * texture is tracked in a registry and its layers handed out by a slotmap. */
    if (gpu_registry_init(&csm->registry, 8u) != 0)
        return false;
    shadow_atlas_config_t sac = {
        .loader = loader,
        .registry = &csm->registry,
        .resolution = csm->static_res,
        .layers = csm->cascades,
        .internal_format = GL_R32F, /* plain linear depth for PCSS. */
    };
    if (!shadow_atlas_init(&csm->static_atlas, &sac))
        return false;
    csm->static_base = shadow_atlas_alloc(&csm->static_atlas, csm->cascades);
    if (csm->static_base < 0)
        return false;

    /* Single low-res dynamic-caster distance map (still a plain 2D target). */
    sc_make_dyn(csm, csm->dynamic_res);

    csm->glGenFramebuffers(1, &csm->fbo);
    csm->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    csm->static_valid = false;
    return true;
}

void shadow_csm_destroy(shadow_csm_t *csm)
{
    if (csm == NULL)
        return;
    if (csm->glDeleteFramebuffers && csm->fbo)
        csm->glDeleteFramebuffers(1, &csm->fbo);
    if (csm->glDeleteRenderbuffers && csm->dyn_depth_rb)
        csm->glDeleteRenderbuffers(1, &csm->dyn_depth_rb);
    if (csm->glDeleteTextures && csm->dyn_map)
        csm->glDeleteTextures(1, &csm->dyn_map);
    /* Translucency mask (rpg-29zj) teardown -- all fields zero when the mask
     * was never enabled, so the deletes are safely skipped. */
    if (csm->glDeleteFramebuffers && csm->mask_fbo)
        csm->glDeleteFramebuffers(1, &csm->mask_fbo);
    if (csm->glDeleteRenderbuffers && csm->mask_depth_rb)
        csm->glDeleteRenderbuffers(1, &csm->mask_depth_rb);
    if (csm->glDeleteRenderbuffers && csm->dyn_mask_depth_rb)
        csm->glDeleteRenderbuffers(1, &csm->dyn_mask_depth_rb);
    if (csm->glDeleteTextures && csm->dyn_mask_color)
        csm->glDeleteTextures(1, &csm->dyn_mask_color);
    if (csm->glDeleteTextures && csm->dyn_mask_depth)
        csm->glDeleteTextures(1, &csm->dyn_mask_depth);
    if (csm->mask_enabled) {
        shadow_atlas_destroy(&csm->mask_color_atlas);
        shadow_atlas_destroy(&csm->mask_depth_atlas);
        shader_program_destroy(&csm->mask_shader);
    }
    csm->mask_fbo = csm->mask_depth_rb = csm->dyn_mask_depth_rb = 0;
    csm->dyn_mask_color = csm->dyn_mask_depth = 0;
    csm->mask_enabled = csm->mask_static_valid = false;

    shadow_atlas_destroy(&csm->static_atlas);
    gpu_registry_destroy(&csm->registry);
    shader_program_destroy(&csm->shader);
    csm->fbo = csm->dyn_depth_rb = csm->dyn_map = 0;
    csm->static_valid = false;
}

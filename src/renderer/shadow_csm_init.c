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
/* EVSM2 (positive-exponential variance shadow map). The normalised depth is
 * warped by exp(C*(d-1)) -- a monotonic map into (0,1] that stays hardware-
 * filterable (so no overflow, plain glClearColor works) yet pushes occluder and
 * receiver far apart, turning the variance compare into a near-binary threshold.
 * Store the two moments (w, w^2). Keep EVSM_C in sync with the receiver. */
static const char *const SC_FS =
    "#version 330 core\n"
    "in vec3 v_world;\n"
    "uniform vec3 u_eye;\n"
    "uniform float u_far;\n"
    "layout(location=0) out vec2 o_moments;\n"
    /* C=30: exp(30)^2 = exp(60) ~ 1e26, well under FLT_MAX, with plenty of\n"
     * mantissa left to separate nearby depths. Depth normalised + clamped first. */
    "const float EVSM_C = 30.0;\n"
    "void main(){ float d = clamp(distance(v_world, u_eye) / u_far, 0.0, 1.0);\n"
    "  float w = exp(EVSM_C * d);\n"
    "  o_moments = vec2(w, w*w); }\n";

#define SC_LOAD(dst, name)                                                    \
    do {                                                                      \
        void *p_ = loader->get_proc_address((name), loader->user_data);       \
        if (p_ == NULL) return false;                                         \
        memcpy(&(dst), &p_, sizeof(p_));                                      \
    } while (0)

/* Allocate one RGBA32F 2D-array texture (the EVSM4 moments) with `layers` layers
 * at `res`. Linear + mipmapped so the moments hardware-filter for soft,
 * anti-aliased shadows. */
static void sc_make_array(shadow_csm_t *csm, uint32_t tex, uint32_t res,
                          uint32_t layers)
{
    /* RG32F: two 32-bit float moments. Full float precision (the warp needs it)
     * and RG is universally colour-renderable, unlike RGBA32F. */
    csm->glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
    csm->glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RG32F, (int32_t)res,
                      (int32_t)res, (int32_t)layers, 0, GL_RG, GL_FLOAT, NULL);
    csm->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER,
                         GL_LINEAR_MIPMAP_LINEAR);
    csm->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    csm->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    csm->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    /* Allocate the mip chain now (glGenerateMipmap after each render fills it). */
    csm->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 4);
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
    SC_LOAD(csm->glFramebufferRenderbuffer, "glFramebufferRenderbuffer");
    SC_LOAD(csm->glGenTextures, "glGenTextures");
    SC_LOAD(csm->glDeleteTextures, "glDeleteTextures");
    SC_LOAD(csm->glBindTexture, "glBindTexture");
    SC_LOAD(csm->glActiveTexture, "glActiveTexture");
    SC_LOAD(csm->glGenerateMipmap, "glGenerateMipmap");
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

    csm->glGenTextures(1, &csm->static_array);
    sc_make_array(csm, csm->static_array, csm->static_res, csm->cascades);
    csm->glGenTextures(1, &csm->dynamic_array);
    sc_make_array(csm, csm->dynamic_array, csm->dynamic_res, csm->cascades);

    /* One depth renderbuffer per resolution (reused across cascade layers, one
     * layer rendered at a time). */
    csm->glGenRenderbuffers(1, &csm->depth_rb_static);
    csm->glBindRenderbuffer(GL_RENDERBUFFER, csm->depth_rb_static);
    csm->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                               (int32_t)csm->static_res, (int32_t)csm->static_res);
    csm->glGenRenderbuffers(1, &csm->depth_rb_dynamic);
    csm->glBindRenderbuffer(GL_RENDERBUFFER, csm->depth_rb_dynamic);
    csm->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                               (int32_t)csm->dynamic_res, (int32_t)csm->dynamic_res);

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
    if (csm->glDeleteRenderbuffers) {
        if (csm->depth_rb_static)
            csm->glDeleteRenderbuffers(1, &csm->depth_rb_static);
        if (csm->depth_rb_dynamic)
            csm->glDeleteRenderbuffers(1, &csm->depth_rb_dynamic);
    }
    if (csm->glDeleteTextures) {
        if (csm->static_array)
            csm->glDeleteTextures(1, &csm->static_array);
        if (csm->dynamic_array)
            csm->glDeleteTextures(1, &csm->dynamic_array);
    }
    shader_program_destroy(&csm->shader);
    csm->fbo = csm->depth_rb_static = csm->depth_rb_dynamic = 0;
    csm->static_array = csm->dynamic_array = 0;
    csm->static_valid = false;
}

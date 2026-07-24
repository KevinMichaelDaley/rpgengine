/**
 * @file refl_bake.h
 * @brief GL bake pass for reflection probes (rpg-akwc): rasterize the whole
 *        scene into a probe-centred cube (single pass over the draw list per
 *        face, lit with sun + a hemispherical irradiance term + emissive),
 *        read the faces back, then CPU-assemble the octahedral atlas
 *        (progressive prefilter + SDF specular-occlusion alpha) and write
 *        the .rprobe sidecar.
 *
 * Ownership: refl_bake_init owns GL objects until refl_bake_destroy; the
 * face readback buffers in refl_bake_probe are caller-owned. The bake is a
 * one-shot offline pass -- heap allocation inside refl_bake_run is
 * deliberate and documented (never per-frame).
 */
#ifndef FERRUM_RENDERER_GI_REFL_BAKE_H
#define FERRUM_RENDERER_GI_REFL_BAKE_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/render_scene.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Bake knobs (all validated/clamped inside refl_bake_run). */
typedef struct refl_bake_params {
    float spacing;        /**< probe grid spacing in metres (default 12). */
    uint32_t tile_res;    /**< octa tile edge at mip 0 (default 64). */
    uint32_t mips;        /**< filtered mips, <= REFL_PROBE_MAX_MIPS (5). */
    uint32_t face_res;    /**< cube face resolution (default 64). */
    uint32_t max_probes;  /**< probe cap (default 256). */
    float min_clear;      /**< SDF clearance for placement (default 0.75). */
    float sun_dir[3];     /**< world direction TOWARD the sun. */
    float sun_color[3];   /**< sun radiance. */
    float ambient[3];     /**< sky irradiance for the hemispherical term. */
    uint32_t depth_res;   /**< octa visibility-depth tile edge (default 16). */
    float sky[3];         /**< clear/sky radiance for full-pipeline faces
                           *   (what unrendered directions reflect). */
    /**
     * Optional FULL-PIPELINE face renderer: when set, each cube face is
     * rendered by this callback INSTEAD of the built-in minimal lit shader
     * -- bind @p fbo as the pipeline target, render the scene with the
     * given camera (column-major view/proj, eye, square face_res viewport)
     * and leave the result in the fbo attachments. This is how the bake
     * gets the real forward shader (shadows, lightmaps, GI, textures) for
     * mirror reflections; the readback then inverts the forward gamma to
     * store linear radiance.
     */
    void (*render_fn)(void *user, uint32_t fbo, const float view[16],
                      const float proj[16], const float eye[3],
                      uint32_t face_res, float sun_vis,
                      const float ambient[3]);
    void *render_user;    /**< passed through to render_fn. */
    /* Optional injected SDF sampler (placement / occlusion / sun-vis):
     * when NULL the run opens <sdf_prefix>_cNNN.sdf itself. */
    float (*sdf_fn)(const float p[3], void *user);
    void *sdf_user;
    const float *place_min;  /**< optional placement AABB override (min). */
    const float *place_max;  /**< optional placement AABB override (max). */
    const char *out_path;    /**< optional output path override. */
} refl_bake_params_t;

/** GL state for the cube pass (function pointers + FBO + shader). */
typedef struct refl_bake {
    shader_program_t shader;
    shader_uniform_cache_t cache;
    uint32_t fbo;
    uint32_t color;      /**< RGBA32F face target. */
    uint32_t depth_rb;
    uint32_t face_res;
    /* GL entry points (loaded in init; see the SC_LOAD pattern). */
    void (*glGenFramebuffers)(int32_t, uint32_t *);
    void (*glDeleteFramebuffers)(int32_t, const uint32_t *);
    void (*glBindFramebuffer)(uint32_t, uint32_t);
    void (*glFramebufferTexture2D)(uint32_t, uint32_t, uint32_t, uint32_t,
                                   int32_t);
    void (*glGenTextures)(int32_t, uint32_t *);
    void (*glDeleteTextures)(int32_t, const uint32_t *);
    void (*glBindTexture)(uint32_t, uint32_t);
    void (*glTexImage2D)(uint32_t, int32_t, int32_t, int32_t, int32_t,
                         int32_t, uint32_t, uint32_t, const void *);
    void (*glTexParameteri)(uint32_t, uint32_t, int32_t);
    void (*glGenRenderbuffers)(int32_t, uint32_t *);
    void (*glDeleteRenderbuffers)(int32_t, const uint32_t *);
    void (*glBindRenderbuffer)(uint32_t, uint32_t);
    void (*glRenderbufferStorage)(uint32_t, uint32_t, int32_t, int32_t);
    void (*glFramebufferRenderbuffer)(uint32_t, uint32_t, uint32_t,
                                      uint32_t);
    void (*glViewport)(int32_t, int32_t, int32_t, int32_t);
    void (*glClearColor)(float, float, float, float);
    void (*glClear)(uint32_t);
    void (*glEnable)(uint32_t);
    void (*glDisable)(uint32_t);
    void (*glDepthFunc)(uint32_t);
    void (*glDrawElements)(uint32_t, int32_t, uint32_t, const void *);
    void (*glReadPixels)(int32_t, int32_t, int32_t, int32_t, uint32_t,
                         uint32_t, void *);
    void (*glFinish)(void);
} refl_bake_t;

/**
 * Create the face target + lit bake shader. Returns false on NULL args or
 * GL/shader failure (partial state cleaned up).
 */
bool refl_bake_init(refl_bake_t *rb, const gl_loader_t *loader,
                    uint32_t face_res);

/**
 * Render the scene from @p pos into six RGBA32F faces (GL face order),
 * each face_res^2*4 floats into @p faces[f] (caller-owned, non-NULL).
 * Via prm->render_fn when set (the full forward pipeline); otherwise the
 * built-in minimal shader: albedo(tint) * (sun N.L * @p sun_vis +
 * hemispherical ambient) + emissive, where @p sun_vis is the SDF sun
 * visibility at the probe (enclosed probes must not bake a false
 * unshadowed sun). @p depth_faces (nullable, six face_res^2 float
 * buffers) receives each face's RAW hardware depth for the
 * visibility-depth bake.
 */
void refl_bake_probe(refl_bake_t *rb, const render_scene_t *scene,
                     const float pos[3], const refl_bake_params_t *prm,
                     float sun_vis, float *faces[6],
                     float *depth_faces[6]);

/** Destroy GL objects; NULL-safe, idempotent. */
void refl_bake_destroy(refl_bake_t *rb);

/**
 * Full offline bake: place probes over the scene AABB (culled by the
 * chunked SDF at @p sdf_prefix when present), render + filter + occlusion,
 * write "<sdf_prefix>.rprobe". Returns false when nothing could be baked.
 * Allocates transient CPU buffers with malloc (offline one-shot).
 */
bool refl_bake_run(const gl_loader_t *loader, const render_scene_t *scene,
                   const char *sdf_prefix, const refl_bake_params_t *prm);

/**
 * Bake-mode entry (rpg-wlh9): per-SDF-chunk DENSE probe grids over the
 * lightmap mesh set. For every <sdf_prefix>_cNNN.sdf chunk: place a grid
 * (prm->spacing, default 2.5 m) inside the chunk bounds, render each
 * probe's cube from the lm meshes (minimal lit shader; @p sun nullable),
 * and write <sdf_prefix>_cNNN.rprobe. Chunks with no clear positions
 * write nothing. Returns false when no chunk produced probes.
 */
struct lm_mesh;
struct lm_light;
bool refl_bake_chunks(const gl_loader_t *loader,
                      const struct lm_mesh *meshes, uint32_t n_meshes,
                      const struct lm_light *sun, const float sky[3],
                      const char *sdf_prefix,
                      const refl_bake_params_t *prm);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_GI_REFL_BAKE_H */

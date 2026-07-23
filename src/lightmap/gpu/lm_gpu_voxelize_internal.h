/**
 * @file lm_gpu_voxelize_internal.h
 * @brief Shared internals of the bake-side GPU voxel rasterizer (rpg-bpiz):
 *        the loaded GL table, program/FBO handles and the residency helpers
 *        used by the dense run, the slice rasterizer and the point sampler.
 *        PRIVATE to src/lightmap/gpu/.
 *
 * Mechanism: SLICED RENDER TARGETS. The channel volumes are 3D textures whose
 * layers are attached as MRT color targets one slice at a time; the ENTIRE
 * mesh set is drawn per slice with hardware clip planes (gl_ClipDistance)
 * bounding that slice's slab, so the clipper hands every slice exactly the
 * geometry crossing it. Accumulation is plain float ROP blending (ADD for
 * area/albedo/emissive/normal/occupancy, MIN for transmission) -- no image
 * atomics, no shader-side rasterization.
 */
#ifndef FERRUM_LIGHTMAP_GPU_LM_GPU_VOXELIZE_INTERNAL_H
#define FERRUM_LIGHTMAP_GPU_LM_GPU_VOXELIZE_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ferrum/lightmap/lm_mesh.h"

/* ── GL constants + types (no glad; headless lib) ── */
typedef unsigned int  GLenum, GLuint, GLbitfield;
typedef int           GLint, GLsizei;
typedef unsigned char GLboolean;
typedef char          GLchar;
typedef float         GLfloat;
typedef intptr_t      GLintptr;
typedef ptrdiff_t     GLsizeiptr;

#define GLV_VERTEX_SHADER        0x8B31
#define GLV_FRAGMENT_SHADER      0x8B30
#define GLV_COMPUTE_SHADER       0x91B9
#define GLV_COMPILE_STATUS       0x8B81
#define GLV_LINK_STATUS          0x8B82
#define GLV_ARRAY_BUFFER         0x8892
#define GLV_ELEMENT_ARRAY_BUFFER 0x8893
#define GLV_SHADER_STORAGE_BUFFER 0x90D2
#define GLV_STATIC_DRAW          0x88E4
#define GLV_DYNAMIC_DRAW         0x88E8
#define GLV_TRIANGLES            0x0004
#define GLV_UNSIGNED_INT         0x1405
#define GLV_UNSIGNED_BYTE        0x1401
#define GLV_FLOAT                0x1406
#define GLV_TEXTURE_2D           0x0DE1
#define GLV_TEXTURE_3D           0x806F
#define GLV_TEXTURE0             0x84C0
#define GLV_TEXTURE_MIN_FILTER   0x2801
#define GLV_TEXTURE_MAG_FILTER   0x2800
#define GLV_TEXTURE_WRAP_S       0x2802
#define GLV_TEXTURE_WRAP_T       0x2803
#define GLV_TEXTURE_MAX_LEVEL    0x813D
#define GLV_LINEAR               0x2601
#define GLV_NEAREST              0x2600
#define GLV_REPEAT               0x2901
#define GLV_R32F                 0x822E
#define GLV_RGBA32F              0x8814
#define GLV_RED                  0x1903
#define GLV_RGB                  0x1907
#define GLV_RGBA                 0x1908
#define GLV_RGB8                 0x8051
#define GLV_RGBA8                0x8058
#define GLV_SRGB8                0x8C41
#define GLV_SRGB8_ALPHA8         0x8C43
#define GLV_UNPACK_ALIGNMENT     0x0CF5
#define GLV_PACK_ALIGNMENT       0x0D05
#define GLV_FRAMEBUFFER          0x8D40
#define GLV_COLOR_ATTACHMENT0    0x8CE0
#define GLV_FRAMEBUFFER_COMPLETE 0x8CD5
#define GLV_COLOR                0x1800
#define GLV_BLEND                0x0BE2
#define GLV_FUNC_ADD             0x8006
#define GLV_MIN                  0x8007
#define GLV_ONE                  1
#define GLV_CLIP_DISTANCE0       0x3000
#define GLV_CLIP_DISTANCE1       0x3001
#define GLV_DEPTH_TEST           0x0B71
#define GLV_CULL_FACE            0x0B44
#define GLV_VIEWPORT             0x0BA2
#define GLV_ALL_BARRIER_BITS     0xFFFFFFFFu

/* Channel targets (MRT attachment order). */
#define LM_VOX_CH_AREA  0   /**< R32F: accumulated surface area. */
#define LM_VOX_CH_ALB   1   /**< RGBA32F: albedo*area sum, A = coverage count. */
#define LM_VOX_CH_EMI   2   /**< RGBA32F: emissive*area sum. */
#define LM_VOX_CH_NRM   3   /**< RGBA32F: normal*area sum. */
#define LM_VOX_CH_TRANS 4   /**< R32F: transmission, MIN-blended (1 = clear). */
#define LM_VOX_CHANNELS 5

/* GPU window budget (cells per channel set; ~56 B/cell across channels). */
#define LM_VOX_BUDGET_CELLS (8u * 1024u * 1024u)
/* Cubic tile edge for the full-resolution point sampler. */
#define LM_VOX_TILE 128

/* Loaded GL entry points (filled by lm_gpu_voxelize_init). */
typedef struct lm_voxi_gl {
    GLuint (*CreateShader)(GLenum);
    void   (*ShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *);
    void   (*CompileShader)(GLuint);
    void   (*GetShaderiv)(GLuint, GLenum, GLint *);
    void   (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
    GLuint (*CreateProgram)(void);
    void   (*AttachShader)(GLuint, GLuint);
    void   (*LinkProgram)(GLuint);
    void   (*GetProgramiv)(GLuint, GLenum, GLint *);
    void   (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
    void   (*DeleteShader)(GLuint);
    void   (*DeleteProgram)(GLuint);
    void   (*UseProgram)(GLuint);
    GLint  (*GetUniformLocation)(GLuint, const GLchar *);
    void   (*Uniform1i)(GLint, GLint);
    void   (*Uniform1f)(GLint, GLfloat);
    void   (*Uniform2f)(GLint, GLfloat, GLfloat);
    void   (*Uniform3f)(GLint, GLfloat, GLfloat, GLfloat);
    void   (*Uniform3i)(GLint, GLint, GLint, GLint);
    void   (*GenBuffers)(GLsizei, GLuint *);
    void   (*DeleteBuffers)(GLsizei, const GLuint *);
    void   (*BindBuffer)(GLenum, GLuint);
    void   (*BufferData)(GLenum, GLsizeiptr, const void *, GLenum);
    void   (*BindBufferBase)(GLenum, GLuint, GLuint);
    void   (*GetBufferSubData)(GLenum, GLintptr, GLsizeiptr, void *);
    void   (*GenVertexArrays)(GLsizei, GLuint *);
    void   (*DeleteVertexArrays)(GLsizei, const GLuint *);
    void   (*BindVertexArray)(GLuint);
    void   (*EnableVertexAttribArray)(GLuint);
    void   (*VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei,
                                  const void *);
    void   (*DrawElements)(GLenum, GLsizei, GLenum, const void *);
    void   (*GenTextures)(GLsizei, GLuint *);
    void   (*DeleteTextures)(GLsizei, const GLuint *);
    void   (*BindTexture)(GLenum, GLuint);
    void   (*ActiveTexture)(GLenum);
    void   (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum,
                         GLenum, const void *);
    void   (*TexImage3D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint,
                         GLenum, GLenum, const void *);
    void   (*TexParameteri)(GLenum, GLenum, GLint);
    void   (*GetTexImage)(GLenum, GLint, GLenum, GLenum, void *);
    void   (*PixelStorei)(GLenum, GLint);
    void   (*GenFramebuffers)(GLsizei, GLuint *);
    void   (*DeleteFramebuffers)(GLsizei, const GLuint *);
    void   (*BindFramebuffer)(GLenum, GLuint);
    GLenum (*CheckFramebufferStatus)(GLenum);
    void   (*FramebufferTextureLayer)(GLenum, GLenum, GLuint, GLint, GLint);
    void   (*DrawBuffers)(GLsizei, const GLenum *);
    void   (*ClearBufferfv)(GLenum, GLint, const GLfloat *);
    void   (*BlendEquationi)(GLuint, GLenum);
    void   (*BlendFunc)(GLenum, GLenum);
    void   (*MemoryBarrier)(GLbitfield);
    void   (*DispatchCompute)(GLuint, GLuint, GLuint);
    void   (*Viewport)(GLint, GLint, GLsizei, GLsizei);
    void   (*Enable)(GLenum);
    void   (*Disable)(GLenum);
    GLboolean (*IsEnabled)(GLenum);
    void   (*DepthMask)(GLboolean);
    void   (*GetIntegerv)(GLenum, GLint *);
    void   (*Finish)(void);
} lm_voxi_gl_t;

extern lm_voxi_gl_t lm_voxi_gl;
extern GLuint lm_voxi_prog;    /**< slice raster VS+FS (MRT) */
extern GLuint lm_voxi_sample;  /**< point sampler CS (lazy-compiled) */
extern GLuint lm_voxi_fbo;     /**< slice FBO */
extern bool   lm_voxi_ready;

/* Per-run GPU residency for one mesh (world AABB cached for slab culling). */
typedef struct lm_voxi_mesh {
    GLuint vao, vbo, ebo, alb_tex, emi_tex;
    const lm_mesh_t *src;
    float bb_min[3], bb_max[3];
} lm_voxi_mesh_t;

/** A box-culled, GPU-resident mesh set with its deduped material textures. */
typedef struct lm_voxi_scene {
    lm_voxi_mesh_t   *gm;
    const lm_image_t **imgs;
    GLuint           *img_tex;
    uint32_t          n_gm, n_img;
} lm_voxi_scene_t;

/** Upload every mesh overlapping [bmin,bmax] + its textures. False = OOM/GL
 *  failure (partial residency already released). */
bool lm_voxi_scene_upload(const lm_mesh_t *meshes, uint32_t n_meshes,
                          const float bmin[3], const float bmax[3],
                          lm_voxi_scene_t *out);
/** Release a scene's GL objects + arrays. Safe on a zeroed struct. */
void lm_voxi_scene_free(lm_voxi_scene_t *s);

/** Upload one mesh (interleaved pos/nrm/uv VBO + EBO + VAO, AABB cached). */
bool lm_voxi_upload_mesh(const lm_mesh_t *m, lm_voxi_mesh_t *out);
/** Delete a mesh's GL objects (textures are owned by the image cache). */
void lm_voxi_free_mesh(lm_voxi_mesh_t *gm);
/** Upload an lm_image as a GL texture (sRGB-decoding format); 0 on failure. */
GLuint lm_voxi_upload_image(const lm_image_t *img);
/** Mesh world AABB (from positions) overlaps [bmin,bmax]? */
bool lm_voxi_mesh_overlaps(const lm_mesh_t *m, const float bmin[3],
                           const float bmax[3]);

/** One channel-volume set for a window: layers slice along @p axis, texel
 *  (u,v,layer) with (u,v) the two non-axis grid dims in ascending order. */
typedef struct lm_voxi_vols {
    GLuint tex[LM_VOX_CHANNELS];
    int    udim, vdim, layers;   /**< texture extents (layers = dims[axis]). */
    int    axis;                 /**< world axis the layers slice along. */
} lm_voxi_vols_t;

/** Create the channel textures for a window sliced along @p axis. */
bool lm_voxi_vols_create(lm_voxi_vols_t *v, const int dims[3], int axis);
/** Delete the channel textures. */
void lm_voxi_vols_free(lm_voxi_vols_t *v);
/** Read one channel back (comps = 1 or 4 floats per texel, caller-sized). */
void lm_voxi_vols_read(const lm_voxi_vols_t *v, int channel, int comps,
                       float *dst);

/**
 * Rasterize @p gm meshes into @p vols over the window box (@p origin/@p ext,
 * @p dims cells): for every layer, attach the five channel slices as MRT,
 * clear them (transmission to 1), set the hardware clip planes to the slab
 * and draw every mesh whose bounds cross it. Float blending accumulates (ADD;
 * MIN for transmission). The caller owns state save/restore around windows.
 */
void lm_voxi_raster_window(const lm_voxi_mesh_t *gm, uint32_t n_gm,
                           const float origin[3], const float ext[3],
                           const int dims[3], const lm_voxi_vols_t *vols);

#endif /* FERRUM_LIGHTMAP_GPU_LM_GPU_VOXELIZE_INTERNAL_H */

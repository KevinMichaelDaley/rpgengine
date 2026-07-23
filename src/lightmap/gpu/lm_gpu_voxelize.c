/**
 * @file lm_gpu_voxelize.c
 * @brief Bake-side GPU voxel rasterizer (see lm_gpu_voxelize.h, rpg-bpiz).
 *
 * The mechanism is the runtime dynamic-object volume rasterizer's
 * (gi_voxelize_draw.c): attachment-less FBO, the destination volume bound as a
 * LAYERED image (one sliced render target), fragment-shader image writes at the
 * voxel of the fragment's world position. Bake outputs need accumulation, so
 * the volume is one r32ui texture whose z extent carries LM_VOX_PLANES
 * fixed-point channel slabs written with imageAtomic*.
 */
#include "ferrum/lightmap/gpu/lm_gpu_voxelize.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#define GLV_STATIC_DRAW          0x88E4
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
#define GLV_R32UI                0x8236
#define GLV_RED_INTEGER          0x8D94
#define GLV_RGB                  0x1907
#define GLV_RGBA                 0x1908
#define GLV_RGB8                 0x8051
#define GLV_RGBA8                0x8058
#define GLV_SRGB8                0x8C41
#define GLV_SRGB8_ALPHA8         0x8C43
#define GLV_UNPACK_ALIGNMENT     0x0CF5
#define GLV_FRAMEBUFFER          0x8D40
#define GLV_FRAMEBUFFER_DEFAULT_WIDTH  0x9310
#define GLV_FRAMEBUFFER_DEFAULT_HEIGHT 0x9311
#define GLV_DEPTH_TEST           0x0B71
#define GLV_CULL_FACE            0x0B44
#define GLV_VIEWPORT             0x0BA2
#define GLV_READ_WRITE           0x88BA
#define GLV_ALL_BARRIER_BITS     0xFFFFFFFFu

/* Fixed-point channel planes stacked along z (see header). */
#define LM_VOX_PLANES 12
#define LM_VOX_FXA 32768.0f  /* area / albedo / normal scale */
#define LM_VOX_FXE 16384.0f  /* emissive scale (HDR headroom) */
#define LM_VOX_FXT 65535u    /* transmission scale (init = clear) */
/* GPU volume budget: cells * LM_VOX_PLANES * 4 B; 512 MB -> ~10.6M cells. */
#define LM_VOX_BUDGET_CELLS (10u * 1024u * 1024u)

static struct {
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
    void   (*Uniform1ui)(GLint, GLuint);
    void   (*Uniform2f)(GLint, GLfloat, GLfloat);
    void   (*Uniform3f)(GLint, GLfloat, GLfloat, GLfloat);
    void   (*Uniform3i)(GLint, GLint, GLint, GLint);
    void   (*GenBuffers)(GLsizei, GLuint *);
    void   (*DeleteBuffers)(GLsizei, const GLuint *);
    void   (*BindBuffer)(GLenum, GLuint);
    void   (*BufferData)(GLenum, GLsizeiptr, const void *, GLenum);
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
    void   (*FramebufferParameteri)(GLenum, GLenum, GLint);
    void   (*BindImageTexture)(GLuint, GLuint, GLint, GLboolean, GLint, GLenum,
                               GLenum);
    void   (*MemoryBarrier)(GLbitfield);
    void   (*DispatchCompute)(GLuint, GLuint, GLuint);
    void   (*Viewport)(GLint, GLint, GLsizei, GLsizei);
    void   (*Enable)(GLenum);
    void   (*Disable)(GLenum);
    GLboolean (*IsEnabled)(GLenum);
    void   (*DepthMask)(GLboolean);
    void   (*GetIntegerv)(GLenum, GLint *);
    void   (*Finish)(void);
} gl;

static GLuint g_prog, g_clear, g_fbo;
static bool g_ready;

/* Raster VS: the gi_voxelize flattening -- normalise the world position over
 * the box and project the two non-@p axis components onto the raster plane.
 * Meshes are already world-space, so there is no model matrix. */
static const char *const VOX_VS =
    "#version 430 core\n"
    "layout(location=0) in vec3 in_pos;\n"
    "layout(location=1) in vec3 in_nrm;\n"
    "layout(location=2) in vec2 in_uv;\n"
    "uniform vec3 u_origin; uniform vec3 u_extent; uniform int u_axis;\n"
    "out vec3 v_world; out vec3 v_nrm; out vec2 v_uv;\n"
    "void main(){\n"
    "  v_world = in_pos; v_nrm = in_nrm; v_uv = in_uv;\n"
    "  vec3 n = (in_pos - u_origin) / max(u_extent, vec3(1e-6));\n"
    "  vec2 xy = (u_axis==0) ? n.yz : ((u_axis==1) ? n.xz : n.xy);\n"
    "  gl_Position = vec4(xy * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

/* Raster FS: voxel from the world position, channel planes along z.
 * Occupancy + transmission take EVERY covering fragment (union over the three
 * projections = hole-free); the accumulated channels take only fragments whose
 * geometric normal is dominant along the current raster axis, so each surface
 * point contributes exactly once with its best-sampled projection. */
static const char *const VOX_FS =
    "#version 430 core\n"
    "layout(r32ui, binding=0) uniform coherent uimage3D u_vol;\n"
    "in vec3 v_world; in vec3 v_nrm; in vec2 v_uv;\n"
    "uniform vec3 u_origin; uniform vec3 u_extent;\n"
    "uniform ivec3 u_dims; uniform int u_zbase; uniform int u_zslab;\n"
    "uniform int u_axis; uniform vec2 u_frag_sz;\n"
    "uniform vec3 u_alb_tint; uniform vec3 u_emi_tint; uniform uint u_trans_fx;\n"
    "uniform int u_has_alb; uniform int u_has_emi;\n"
    "uniform sampler2D u_alb_map; uniform sampler2D u_emi_map;\n"
    "void main(){\n"
    "  vec3 n = (v_world - u_origin) / max(u_extent, vec3(1e-6));\n"
    "  ivec3 vi = ivec3(floor(n * vec3(u_dims)));\n"
    "  if (any(lessThan(vi, ivec3(0))) || any(greaterThanEqual(vi, u_dims)))\n"
    "    return;\n"
    "  if (vi.z < u_zbase || vi.z >= u_zbase + u_zslab) return;\n"
    "  ivec3 c = ivec3(vi.x, vi.y, vi.z - u_zbase);\n"
    "  imageAtomicOr (u_vol, c + ivec3(0,0, 0*u_zslab), 1u);\n"
    "  imageAtomicMin(u_vol, c + ivec3(0,0,11*u_zslab), u_trans_fx);\n"
    "  vec3 gn = cross(dFdx(v_world), dFdy(v_world));\n"
    "  vec3 an = abs(gn);\n"
    "  int dom = (an.x >= an.y && an.x >= an.z) ? 0 : ((an.y >= an.z) ? 1 : 2);\n"
    "  if (dom != u_axis) return;\n"
    "  float gl2 = max(length(gn), 1e-12);\n"
    "  float cosw = max(an[u_axis] / gl2, 0.25);\n"
    "  float w = (u_frag_sz.x * u_frag_sz.y) / cosw;\n"
    "  vec3 nrm = (dot(v_nrm, v_nrm) > 1e-8) ? normalize(v_nrm) : (gn / gl2);\n"
    "  vec3 alb = u_alb_tint * ((u_has_alb != 0)\n"
    "           ? texture(u_alb_map, v_uv).rgb : vec3(1.0));\n"
    "  vec3 emi = u_emi_tint * ((u_has_emi != 0)\n"
    "           ? texture(u_emi_map, v_uv).rgb : vec3(1.0));\n"
    "  imageAtomicAdd(u_vol, c + ivec3(0,0, 1*u_zslab), uint(w * 32768.0 + 0.5));\n"
    "  imageAtomicAdd(u_vol, c + ivec3(0,0, 2*u_zslab), uint(alb.r * w * 32768.0 + 0.5));\n"
    "  imageAtomicAdd(u_vol, c + ivec3(0,0, 3*u_zslab), uint(alb.g * w * 32768.0 + 0.5));\n"
    "  imageAtomicAdd(u_vol, c + ivec3(0,0, 4*u_zslab), uint(alb.b * w * 32768.0 + 0.5));\n"
    "  imageAtomicAdd(u_vol, c + ivec3(0,0, 5*u_zslab), uint(emi.r * w * 16384.0 + 0.5));\n"
    "  imageAtomicAdd(u_vol, c + ivec3(0,0, 6*u_zslab), uint(emi.g * w * 16384.0 + 0.5));\n"
    "  imageAtomicAdd(u_vol, c + ivec3(0,0, 7*u_zslab), uint(emi.b * w * 16384.0 + 0.5));\n"
    "  vec3 nb = (nrm * 0.5 + 0.5) * w * 32768.0;\n"
    "  imageAtomicAdd(u_vol, c + ivec3(0,0, 8*u_zslab), uint(nb.x + 0.5));\n"
    "  imageAtomicAdd(u_vol, c + ivec3(0,0, 9*u_zslab), uint(nb.y + 0.5));\n"
    "  imageAtomicAdd(u_vol, c + ivec3(0,0,10*u_zslab), uint(nb.z + 0.5));\n"
    "}\n";

/* Compute clear: zero every plane, transmission plane to FXT (= clear). */
static const char *const VOX_CLEAR_CS =
    "#version 430 core\n"
    "layout(local_size_x=4, local_size_y=4, local_size_z=4) in;\n"
    "layout(r32ui, binding=0) uniform writeonly uimage3D u_vol;\n"
    "uniform ivec3 u_wdims; uniform int u_zslab;\n"
    "void main(){\n"
    "  ivec3 p = ivec3(gl_GlobalInvocationID);\n"
    "  if (any(greaterThanEqual(p, u_wdims))) return;\n"
    "  uint v = ((p.z / u_zslab) == 11) ? 65535u : 0u;\n"
    "  imageStore(u_vol, p, uvec4(v, 0u, 0u, 0u));\n"
    "}\n";

static GLuint vox_compile(GLenum type, const char *src)
{
    GLuint s = gl.CreateShader(type);
    gl.ShaderSource(s, 1, &src, NULL);
    gl.CompileShader(s);
    GLint ok = 0;
    gl.GetShaderiv(s, GLV_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl.GetShaderInfoLog(s, (GLsizei)sizeof log, NULL, log);
        fprintf(stderr, "lm_gpu_voxelize: shader compile failed: %s\n", log);
        gl.DeleteShader(s);
        return 0u;
    }
    return s;
}

static GLuint vox_link(GLuint a, GLuint b)
{
    GLuint p = gl.CreateProgram();
    gl.AttachShader(p, a);
    if (b) gl.AttachShader(p, b);
    gl.LinkProgram(p);
    GLint ok = 0;
    gl.GetProgramiv(p, GLV_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl.GetProgramInfoLog(p, (GLsizei)sizeof log, NULL, log);
        fprintf(stderr, "lm_gpu_voxelize: link failed: %s\n", log);
        gl.DeleteProgram(p);
        return 0u;
    }
    return p;
}

bool lm_gpu_voxelize_init(const gl_loader_t *loader)
{
    if (loader == NULL || loader->get_proc_address == NULL) return false;
    if (g_ready) return true;
    memset(&gl, 0, sizeof gl);
#define VX_LOAD(field, name) do {                                              \
    void *p_ = loader->get_proc_address((name), loader->user_data);            \
    if (p_ == NULL) { fprintf(stderr, "lm_gpu_voxelize: missing %s\n", name);  \
                      return false; }                                          \
    memcpy(&gl.field, &p_, sizeof p_); } while (0)
    VX_LOAD(CreateShader, "glCreateShader");
    VX_LOAD(ShaderSource, "glShaderSource");
    VX_LOAD(CompileShader, "glCompileShader");
    VX_LOAD(GetShaderiv, "glGetShaderiv");
    VX_LOAD(GetShaderInfoLog, "glGetShaderInfoLog");
    VX_LOAD(CreateProgram, "glCreateProgram");
    VX_LOAD(AttachShader, "glAttachShader");
    VX_LOAD(LinkProgram, "glLinkProgram");
    VX_LOAD(GetProgramiv, "glGetProgramiv");
    VX_LOAD(GetProgramInfoLog, "glGetProgramInfoLog");
    VX_LOAD(DeleteShader, "glDeleteShader");
    VX_LOAD(DeleteProgram, "glDeleteProgram");
    VX_LOAD(UseProgram, "glUseProgram");
    VX_LOAD(GetUniformLocation, "glGetUniformLocation");
    VX_LOAD(Uniform1i, "glUniform1i");
    VX_LOAD(Uniform1ui, "glUniform1ui");
    VX_LOAD(Uniform2f, "glUniform2f");
    VX_LOAD(Uniform3f, "glUniform3f");
    VX_LOAD(Uniform3i, "glUniform3i");
    VX_LOAD(GenBuffers, "glGenBuffers");
    VX_LOAD(DeleteBuffers, "glDeleteBuffers");
    VX_LOAD(BindBuffer, "glBindBuffer");
    VX_LOAD(BufferData, "glBufferData");
    VX_LOAD(GenVertexArrays, "glGenVertexArrays");
    VX_LOAD(DeleteVertexArrays, "glDeleteVertexArrays");
    VX_LOAD(BindVertexArray, "glBindVertexArray");
    VX_LOAD(EnableVertexAttribArray, "glEnableVertexAttribArray");
    VX_LOAD(VertexAttribPointer, "glVertexAttribPointer");
    VX_LOAD(DrawElements, "glDrawElements");
    VX_LOAD(GenTextures, "glGenTextures");
    VX_LOAD(DeleteTextures, "glDeleteTextures");
    VX_LOAD(BindTexture, "glBindTexture");
    VX_LOAD(ActiveTexture, "glActiveTexture");
    VX_LOAD(TexImage2D, "glTexImage2D");
    VX_LOAD(TexImage3D, "glTexImage3D");
    VX_LOAD(TexParameteri, "glTexParameteri");
    VX_LOAD(GetTexImage, "glGetTexImage");
    VX_LOAD(PixelStorei, "glPixelStorei");
    VX_LOAD(GenFramebuffers, "glGenFramebuffers");
    VX_LOAD(DeleteFramebuffers, "glDeleteFramebuffers");
    VX_LOAD(BindFramebuffer, "glBindFramebuffer");
    VX_LOAD(FramebufferParameteri, "glFramebufferParameteri");
    VX_LOAD(BindImageTexture, "glBindImageTexture");
    VX_LOAD(MemoryBarrier, "glMemoryBarrier");
    VX_LOAD(DispatchCompute, "glDispatchCompute");
    VX_LOAD(Viewport, "glViewport");
    VX_LOAD(Enable, "glEnable");
    VX_LOAD(Disable, "glDisable");
    VX_LOAD(IsEnabled, "glIsEnabled");
    VX_LOAD(DepthMask, "glDepthMask");
    VX_LOAD(GetIntegerv, "glGetIntegerv");
    VX_LOAD(Finish, "glFinish");
#undef VX_LOAD

    GLuint vs = vox_compile(GLV_VERTEX_SHADER, VOX_VS);
    GLuint fs = vs ? vox_compile(GLV_FRAGMENT_SHADER, VOX_FS) : 0u;
    if (!vs || !fs) {
        if (vs) gl.DeleteShader(vs);
        return false;
    }
    g_prog = vox_link(vs, fs);
    gl.DeleteShader(vs);
    gl.DeleteShader(fs);
    GLuint cs = g_prog ? vox_compile(GLV_COMPUTE_SHADER, VOX_CLEAR_CS) : 0u;
    if (cs) {
        g_clear = vox_link(cs, 0u);
        gl.DeleteShader(cs);
    }
    if (!g_prog || !g_clear) {
        lm_gpu_voxelize_shutdown();
        return false;
    }
    gl.GenFramebuffers(1, &g_fbo);
    g_ready = true;
    return true;
}

void lm_gpu_voxelize_shutdown(void)
{
    if (gl.DeleteProgram != NULL) {
        if (g_prog)  gl.DeleteProgram(g_prog);
        if (g_clear) gl.DeleteProgram(g_clear);
        if (g_fbo && gl.DeleteFramebuffers) gl.DeleteFramebuffers(1, &g_fbo);
    }
    g_prog = g_clear = g_fbo = 0u;
    g_ready = false;
}

/* Mesh world AABB overlaps the raster box? */
static bool vox_mesh_overlaps(const lm_mesh_t *m, const phys_aabb_t *box)
{
    if (m->vert_count == 0 || m->positions == NULL) return false;
    float mn[3] = { m->positions[0], m->positions[1], m->positions[2] };
    float mx[3] = { mn[0], mn[1], mn[2] };
    for (uint32_t v = 1; v < m->vert_count; ++v)
        for (int c = 0; c < 3; ++c) {
            float p = m->positions[v * 3 + c];
            if (p < mn[c]) mn[c] = p;
            if (p > mx[c]) mx[c] = p;
        }
    const float bmin[3] = { box->min.x, box->min.y, box->min.z };
    const float bmax[3] = { box->max.x, box->max.y, box->max.z };
    for (int c = 0; c < 3; ++c)
        if (mx[c] < bmin[c] || mn[c] > bmax[c]) return false;
    return true;
}

/* Upload one lm_image as a GL texture (sRGB decoded by the sampler). */
static GLuint vox_upload_image(const lm_image_t *img)
{
    if (img == NULL || img->pixels == NULL || img->width == 0 ||
        img->height == 0 || (img->channels != 3 && img->channels != 4))
        return 0u;
    GLuint t = 0u;
    gl.GenTextures(1, &t);
    gl.BindTexture(GLV_TEXTURE_2D, t);
    gl.PixelStorei(GLV_UNPACK_ALIGNMENT, 1);
    GLint ifmt = img->channels == 4
               ? (img->srgb ? (GLint)GLV_SRGB8_ALPHA8 : (GLint)GLV_RGBA8)
               : (img->srgb ? (GLint)GLV_SRGB8 : (GLint)GLV_RGB8);
    GLenum fmt = img->channels == 4 ? GLV_RGBA : GLV_RGB;
    gl.TexImage2D(GLV_TEXTURE_2D, 0, ifmt, (GLsizei)img->width,
                  (GLsizei)img->height, 0, fmt, GLV_UNSIGNED_BYTE, img->pixels);
    gl.TexParameteri(GLV_TEXTURE_2D, GLV_TEXTURE_MIN_FILTER, GLV_LINEAR);
    gl.TexParameteri(GLV_TEXTURE_2D, GLV_TEXTURE_MAG_FILTER, GLV_LINEAR);
    gl.TexParameteri(GLV_TEXTURE_2D, GLV_TEXTURE_WRAP_S, GLV_REPEAT);
    gl.TexParameteri(GLV_TEXTURE_2D, GLV_TEXTURE_WRAP_T, GLV_REPEAT);
    gl.TexParameteri(GLV_TEXTURE_2D, GLV_TEXTURE_MAX_LEVEL, 0);
    return t;
}

/* Per-run GPU residency for one mesh: interleaved [pos nrm uv] VBO + EBO + VAO
 * + its two (deduped by the caller) material textures. */
typedef struct vox_gpu_mesh {
    GLuint vao, vbo, ebo, alb_tex, emi_tex;
    const lm_mesh_t *src;
} vox_gpu_mesh_t;

static bool vox_upload_mesh(const lm_mesh_t *m, vox_gpu_mesh_t *out)
{
    memset(out, 0, sizeof *out);
    out->src = m;
    float *inter = malloc((size_t)m->vert_count * 8u * sizeof(float));
    if (inter == NULL) return false;
    for (uint32_t v = 0; v < m->vert_count; ++v) {
        float *d = &inter[v * 8u];
        d[0] = m->positions[v * 3 + 0];
        d[1] = m->positions[v * 3 + 1];
        d[2] = m->positions[v * 3 + 2];
        d[3] = m->normals != NULL ? m->normals[v * 3 + 0] : 0.0f;
        d[4] = m->normals != NULL ? m->normals[v * 3 + 1] : 0.0f;
        d[5] = m->normals != NULL ? m->normals[v * 3 + 2] : 0.0f;
        d[6] = m->uv0 != NULL ? m->uv0[v * 2 + 0] : 0.0f;
        d[7] = m->uv0 != NULL ? m->uv0[v * 2 + 1] : 0.0f;
    }
    gl.GenVertexArrays(1, &out->vao);
    gl.BindVertexArray(out->vao);
    gl.GenBuffers(1, &out->vbo);
    gl.BindBuffer(GLV_ARRAY_BUFFER, out->vbo);
    gl.BufferData(GLV_ARRAY_BUFFER,
                  (GLsizeiptr)((size_t)m->vert_count * 8u * sizeof(float)),
                  inter, GLV_STATIC_DRAW);
    free(inter);
    gl.GenBuffers(1, &out->ebo);
    gl.BindBuffer(GLV_ELEMENT_ARRAY_BUFFER, out->ebo);
    gl.BufferData(GLV_ELEMENT_ARRAY_BUFFER,
                  (GLsizeiptr)((size_t)m->index_count * sizeof(uint32_t)),
                  m->indices, GLV_STATIC_DRAW);
    const GLsizei stride = 8 * (GLsizei)sizeof(float);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(0, 3, GLV_FLOAT, 0, stride, (const void *)0);
    gl.EnableVertexAttribArray(1);
    gl.VertexAttribPointer(1, 3, GLV_FLOAT, 0, stride,
                           (const void *)(3u * sizeof(float)));
    gl.EnableVertexAttribArray(2);
    gl.VertexAttribPointer(2, 2, GLV_FLOAT, 0, stride,
                           (const void *)(6u * sizeof(float)));
    gl.BindVertexArray(0u);
    return true;
}

static void vox_free_mesh(vox_gpu_mesh_t *gm)
{
    if (gm->vao) gl.DeleteVertexArrays(1, &gm->vao);
    if (gm->vbo) gl.DeleteBuffers(1, &gm->vbo);
    if (gm->ebo) gl.DeleteBuffers(1, &gm->ebo);
    memset(gm, 0, sizeof *gm);
}

/* Decode one slab of the readback volume into the output grid arrays. */
static void vox_decode_slab(lm_gpu_vox_grid_t *g, const uint32_t *rb,
                            int zbase, int zslab, float vox_area)
{
    const size_t plane = (size_t)g->dims[0] * (size_t)g->dims[1];
    for (int z = 0; z < zslab; ++z)
        for (size_t xy = 0; xy < plane; ++xy) {
            size_t src = (size_t)z * plane + xy;
            size_t dst = (size_t)(zbase + z) * plane + xy;
            const size_t pl = (size_t)zslab * plane;
            float a = (float)rb[src + 1u * pl] / LM_VOX_FXA;
            g->occ[dst]  = rb[src] ? 1u : 0u;
            g->area[dst] = a;
            g->trans[dst] = (float)rb[src + 11u * pl] / (float)LM_VOX_FXT;
            if (a > 0.0f) {
                float inv = 1.0f / (a * LM_VOX_FXA);
                for (int k = 0; k < 3; ++k) {
                    g->albedo[dst * 3 + k] =
                        (float)rb[src + (size_t)(2 + k) * pl] * inv;
                    float nb = (float)rb[src + (size_t)(8 + k) * pl] * inv;
                    g->normal[dst * 3 + k] = nb * 2.0f - 1.0f;
                }
                float nl = sqrtf(g->normal[dst * 3] * g->normal[dst * 3] +
                                 g->normal[dst * 3 + 1] * g->normal[dst * 3 + 1] +
                                 g->normal[dst * 3 + 2] * g->normal[dst * 3 + 2]);
                if (nl > 1e-6f)
                    for (int k = 0; k < 3; ++k) g->normal[dst * 3 + k] /= nl;
            }
            for (int k = 0; k < 3; ++k)
                g->emissive[dst * 3 + k] =
                    (float)rb[src + (size_t)(5 + k) * pl] / LM_VOX_FXE / vox_area;
        }
}

bool lm_gpu_voxelize_run(const lm_mesh_t *meshes, uint32_t n_meshes,
                         const phys_aabb_t *box, const int dims[3],
                         lm_gpu_vox_grid_t *out)
{
    if (!g_ready || box == NULL || dims == NULL || out == NULL ||
        (meshes == NULL && n_meshes > 0))
        return false;
    if (dims[0] < 1 || dims[1] < 1 || dims[2] < 1) return false;

    const float ext[3] = { box->max.x - box->min.x, box->max.y - box->min.y,
                           box->max.z - box->min.z };
    if (ext[0] <= 0.0f || ext[1] <= 0.0f || ext[2] <= 0.0f) return false;

    lm_gpu_vox_grid_t g;
    memset(&g, 0, sizeof g);
    for (int a = 0; a < 3; ++a) {
        g.dims[a] = dims[a];
        g.cell[a] = ext[a] / (float)dims[a];
    }
    g.origin[0] = box->min.x; g.origin[1] = box->min.y; g.origin[2] = box->min.z;
    const size_t n = (size_t)dims[0] * (size_t)dims[1] * (size_t)dims[2];
    g.occ      = calloc(n, sizeof(uint32_t));
    g.area     = calloc(n, sizeof(float));
    g.albedo   = calloc(n * 3u, sizeof(float));
    g.emissive = calloc(n * 3u, sizeof(float));
    g.normal   = calloc(n * 3u, sizeof(float));
    g.trans    = malloc(n * sizeof(float));
    if (!g.occ || !g.area || !g.albedo || !g.emissive || !g.normal || !g.trans) {
        lm_gpu_vox_grid_free(&g);
        return false;
    }
    for (size_t i = 0; i < n; ++i) g.trans[i] = 1.0f;

    /* GPU-resident meshes (culled to the box) + material texture dedupe. */
    vox_gpu_mesh_t *gm = calloc(n_meshes ? n_meshes : 1u, sizeof *gm);
    const lm_image_t **imgs = calloc((size_t)n_meshes * 2u + 1u, sizeof *imgs);
    GLuint *img_tex = calloc((size_t)n_meshes * 2u + 1u, sizeof *img_tex);
    if (gm == NULL || imgs == NULL || img_tex == NULL) {
        free(gm); free((void *)imgs); free(img_tex);
        lm_gpu_vox_grid_free(&g);
        return false;
    }
    uint32_t n_gm = 0, n_img = 0;
    bool ok = true;
    for (uint32_t i = 0; i < n_meshes && ok; ++i) {
        const lm_mesh_t *m = &meshes[i];
        if (m->index_count < 3 || m->positions == NULL || m->indices == NULL)
            continue;
        if (!vox_mesh_overlaps(m, box)) continue;
        ok = vox_upload_mesh(m, &gm[n_gm]);
        if (!ok) break;
        const lm_image_t *want[2] = { m->albedo_image, m->emissive_image };
        GLuint got[2] = { 0u, 0u };
        for (int k = 0; k < 2; ++k) {
            if (want[k] == NULL) continue;
            for (uint32_t j = 0; j < n_img; ++j)
                if (imgs[j] == want[k]) { got[k] = img_tex[j]; break; }
            if (got[k] == 0u) {
                got[k] = vox_upload_image(want[k]);
                if (got[k] != 0u) {
                    imgs[n_img] = want[k];
                    img_tex[n_img] = got[k];
                    ++n_img;
                }
            }
        }
        gm[n_gm].alb_tex = got[0];
        gm[n_gm].emi_tex = got[1];
        ++n_gm;
    }

    /* Saved state (the offline bake owns the context; restore what we touch). */
    GLint vp[4];
    gl.GetIntegerv(GLV_VIEWPORT, vp);
    GLboolean depth_on = gl.IsEnabled(GLV_DEPTH_TEST);
    GLboolean cull_on = gl.IsEnabled(GLV_CULL_FACE);

    /* z-slab loop bounds GPU memory: cells * LM_VOX_PLANES stays under budget. */
    int zslab = (int)(LM_VOX_BUDGET_CELLS /
                      (size_t)((size_t)dims[0] * (size_t)dims[1]));
    if (zslab < 1) zslab = 1;
    if (zslab > dims[2]) zslab = dims[2];

    int vpd = dims[0];
    if (dims[1] > vpd) vpd = dims[1];
    if (dims[2] > vpd) vpd = dims[2];
    vpd *= 2;                                     /* 2x supersampled coverage */
    if (vpd < 64) vpd = 64;
    if (vpd > 2048) vpd = 2048;

    GLuint vol = 0u;
    uint32_t *rb = NULL;
    if (ok) {
        gl.GenTextures(1, &vol);
        gl.BindTexture(GLV_TEXTURE_3D, vol);
        gl.TexParameteri(GLV_TEXTURE_3D, GLV_TEXTURE_MIN_FILTER, GLV_NEAREST);
        gl.TexParameteri(GLV_TEXTURE_3D, GLV_TEXTURE_MAG_FILTER, GLV_NEAREST);
        gl.TexParameteri(GLV_TEXTURE_3D, GLV_TEXTURE_MAX_LEVEL, 0);
        rb = malloc((size_t)dims[0] * (size_t)dims[1] * (size_t)zslab *
                    LM_VOX_PLANES * sizeof(uint32_t));
        ok = rb != NULL;
    }

    const float vox_area = (g.cell[0] * g.cell[1] + g.cell[1] * g.cell[2] +
                            g.cell[0] * g.cell[2]) / 3.0f;
    for (int zbase = 0; zbase < dims[2] && ok; zbase += zslab) {
        int zs = zslab;
        if (zbase + zs > dims[2]) zs = dims[2] - zbase;
        gl.BindTexture(GLV_TEXTURE_3D, vol);
        gl.TexImage3D(GLV_TEXTURE_3D, 0, (GLint)GLV_R32UI, dims[0], dims[1],
                      zs * LM_VOX_PLANES, 0, GLV_RED_INTEGER, GLV_UNSIGNED_INT,
                      NULL);
        gl.BindImageTexture(0, vol, 0, /*layered=*/1, 0, GLV_READ_WRITE,
                            GLV_R32UI);
        /* clear */
        gl.UseProgram(g_clear);
        gl.Uniform3i(gl.GetUniformLocation(g_clear, "u_wdims"),
                     dims[0], dims[1], zs * LM_VOX_PLANES);
        gl.Uniform1i(gl.GetUniformLocation(g_clear, "u_zslab"), zs);
        gl.DispatchCompute((GLuint)((dims[0] + 3) / 4),
                           (GLuint)((dims[1] + 3) / 4),
                           (GLuint)((zs * LM_VOX_PLANES + 3) / 4));
        gl.MemoryBarrier(GLV_ALL_BARRIER_BITS);

        /* raster: attachment-less FBO, one sliced render target (the volume). */
        gl.BindFramebuffer(GLV_FRAMEBUFFER, g_fbo);
        gl.FramebufferParameteri(GLV_FRAMEBUFFER,
                                 GLV_FRAMEBUFFER_DEFAULT_WIDTH, vpd);
        gl.FramebufferParameteri(GLV_FRAMEBUFFER,
                                 GLV_FRAMEBUFFER_DEFAULT_HEIGHT, vpd);
        gl.Viewport(0, 0, vpd, vpd);
        gl.Disable(GLV_DEPTH_TEST);
        gl.DepthMask(0);
        gl.Disable(GLV_CULL_FACE);
        gl.UseProgram(g_prog);
        gl.Uniform3f(gl.GetUniformLocation(g_prog, "u_origin"),
                     g.origin[0], g.origin[1], g.origin[2]);
        gl.Uniform3f(gl.GetUniformLocation(g_prog, "u_extent"),
                     ext[0], ext[1], ext[2]);
        gl.Uniform3i(gl.GetUniformLocation(g_prog, "u_dims"),
                     dims[0], dims[1], dims[2]);
        gl.Uniform1i(gl.GetUniformLocation(g_prog, "u_zbase"), zbase);
        gl.Uniform1i(gl.GetUniformLocation(g_prog, "u_zslab"), zs);
        gl.Uniform1i(gl.GetUniformLocation(g_prog, "u_alb_map"), 0);
        gl.Uniform1i(gl.GetUniformLocation(g_prog, "u_emi_map"), 1);
        for (int axis = 0; axis < 3; ++axis) {
            gl.Uniform1i(gl.GetUniformLocation(g_prog, "u_axis"), axis);
            float fu = (axis == 0 ? ext[1] : ext[0]) / (float)vpd;
            float fv = (axis == 2 ? ext[1] : ext[2]) / (float)vpd;
            gl.Uniform2f(gl.GetUniformLocation(g_prog, "u_frag_sz"), fu, fv);
            for (uint32_t i = 0; i < n_gm; ++i) {
                const lm_mesh_t *m = gm[i].src;
                gl.Uniform3f(gl.GetUniformLocation(g_prog, "u_alb_tint"),
                             m->albedo.x, m->albedo.y, m->albedo.z);
                gl.Uniform3f(gl.GetUniformLocation(g_prog, "u_emi_tint"),
                             m->emissive.x, m->emissive.y, m->emissive.z);
                float tr = 1.0f - m->opacity;
                if (tr < 0.0f) tr = 0.0f;
                if (tr > 1.0f) tr = 1.0f;
                gl.Uniform1ui(gl.GetUniformLocation(g_prog, "u_trans_fx"),
                              (GLuint)(tr * (float)LM_VOX_FXT));
                gl.Uniform1i(gl.GetUniformLocation(g_prog, "u_has_alb"),
                             gm[i].alb_tex != 0u);
                gl.Uniform1i(gl.GetUniformLocation(g_prog, "u_has_emi"),
                             gm[i].emi_tex != 0u);
                gl.ActiveTexture(GLV_TEXTURE0 + 0);
                gl.BindTexture(GLV_TEXTURE_2D, gm[i].alb_tex);
                gl.ActiveTexture(GLV_TEXTURE0 + 1);
                gl.BindTexture(GLV_TEXTURE_2D, gm[i].emi_tex);
                gl.ActiveTexture(GLV_TEXTURE0);
                gl.BindVertexArray(gm[i].vao);
                gl.DrawElements(GLV_TRIANGLES, (GLsizei)m->index_count,
                                GLV_UNSIGNED_INT, NULL);
            }
        }
        gl.BindVertexArray(0u);
        gl.MemoryBarrier(GLV_ALL_BARRIER_BITS);

        gl.BindTexture(GLV_TEXTURE_3D, vol);
        gl.GetTexImage(GLV_TEXTURE_3D, 0, GLV_RED_INTEGER, GLV_UNSIGNED_INT, rb);
        vox_decode_slab(&g, rb, zbase, zs, vox_area);
    }
    gl.Finish();

    /* teardown + state restore */
    free(rb);
    if (vol) gl.DeleteTextures(1, &vol);
    for (uint32_t i = 0; i < n_gm; ++i) vox_free_mesh(&gm[i]);
    for (uint32_t j = 0; j < n_img; ++j)
        if (img_tex[j]) gl.DeleteTextures(1, &img_tex[j]);
    free(gm); free((void *)imgs); free(img_tex);
    gl.UseProgram(0u);
    gl.BindFramebuffer(GLV_FRAMEBUFFER, 0u);
    gl.Viewport(vp[0], vp[1], vp[2], vp[3]);
    gl.DepthMask(1);
    if (depth_on) gl.Enable(GLV_DEPTH_TEST);
    if (cull_on) gl.Enable(GLV_CULL_FACE);

    if (!ok) {
        lm_gpu_vox_grid_free(&g);
        return false;
    }
    *out = g;
    return true;
}

void lm_gpu_vox_grid_free(lm_gpu_vox_grid_t *g)
{
    if (g == NULL) return;
    free(g->occ); free(g->area); free(g->albedo);
    free(g->emissive); free(g->normal); free(g->trans);
    memset(g, 0, sizeof *g);
}

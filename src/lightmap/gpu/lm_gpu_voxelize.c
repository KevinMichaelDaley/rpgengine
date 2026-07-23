/**
 * @file lm_gpu_voxelize.c
 * @brief Bake-side GPU voxel rasterizer (see lm_gpu_voxelize.h, rpg-bpiz).
 *
 * SLICED RENDER TARGETS: the channel volumes' layers are attached as MRT
 * color targets one slice at a time and the ENTIRE mesh set is drawn per
 * slice with hardware clip planes bounding the slab (lm_gpu_voxraster.c), so
 * every slice receives exactly the geometry crossing it from the hardware
 * clipper -- nothing is collapsed onto a single projection box face, and
 * accumulation is plain float ROP blending (no atomics, no shader-side
 * rasterization). Three axis passes union occupancy/transmission (a surface
 * edge-on to one slicing axis is face-on to another) while the material
 * channels take only each fragment's dominant projection, so every surface
 * point contributes exactly once. Dense requests run in z-slab windows to
 * bound GPU memory at full resolution.
 */
#include "ferrum/lightmap/gpu/lm_gpu_voxelize.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lm_gpu_voxelize_internal.h"

/* Slice raster VS: flatten the two non-axis components onto the raster plane
 * (the gi_voxelize projection); the slice's slab is enforced by the HARDWARE
 * clip planes. Meshes are world-space: no model matrix. */
static const char *const VOX_VS =
    "#version 430 core\n"
    "layout(location=0) in vec3 in_pos;\n"
    "layout(location=1) in vec3 in_nrm;\n"
    "layout(location=2) in vec2 in_uv;\n"
    "uniform vec3 u_origin; uniform vec3 u_extent; uniform int u_axis;\n"
    "uniform float u_clip0; uniform float u_clip1;\n"
    "out vec3 v_world; out vec3 v_nrm; out vec2 v_uv;\n"
    "out float gl_ClipDistance[2];\n"
    "void main(){\n"
    "  v_world = in_pos; v_nrm = in_nrm; v_uv = in_uv;\n"
    "  vec3 n = (in_pos - u_origin) / max(u_extent, vec3(1e-6));\n"
    "  vec2 xy = (u_axis==0) ? n.yz : ((u_axis==1) ? n.xz : n.xy);\n"
    "  float ac = (u_axis==0) ? in_pos.x : ((u_axis==1) ? in_pos.y : in_pos.z);\n"
    "  gl_ClipDistance[0] = ac - u_clip0;\n"
    "  gl_ClipDistance[1] = u_clip1 - ac;\n"
    "  gl_Position = vec4(xy * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

/* Slice raster FS: five MRT outputs, accumulated by ROP blending (ADD; MIN on
 * transmission). Occupancy (albedo alpha) + transmission come from every
 * projection; the weighted material channels only from the fragment's
 * dominant projection so each surface point contributes exactly once. */
static const char *const VOX_FS =
    "#version 430 core\n"
    "in vec3 v_world; in vec3 v_nrm; in vec2 v_uv;\n"
    "layout(location=0) out float o_area;\n"
    "layout(location=1) out vec4  o_alb;\n"
    "layout(location=2) out vec4  o_emi;\n"
    "layout(location=3) out vec4  o_nrm;\n"
    "layout(location=4) out float o_trans;\n"
    "uniform int u_axis; uniform vec2 u_cell_uv;\n"
    "uniform vec3 u_alb_tint; uniform vec3 u_emi_tint; uniform float u_trans;\n"
    "uniform int u_has_alb; uniform int u_has_emi;\n"
    "uniform sampler2D u_alb_map; uniform sampler2D u_emi_map;\n"
    "void main(){\n"
    "  vec3 dwx = dFdx(v_world), dwy = dFdy(v_world);\n"
    "  vec3 gn = cross(dwx, dwy);\n"
    "  vec3 an = abs(gn);\n"
    "  int dom = (an.x >= an.y && an.x >= an.z) ? 0 : ((an.y >= an.z) ? 1 : 2);\n"
    "  o_trans = u_trans;\n"
    "  if (dom != u_axis) {\n"
    "    o_area = 0.0; o_alb = vec4(0.0, 0.0, 0.0, 1.0);\n"
    "    o_emi = vec4(0.0); o_nrm = vec4(0.0);\n"
    "    return;\n"
    "  }\n"
    "  float gl2 = max(length(gn), 1e-12);\n"
    "  float cosw = max(an[u_axis] / gl2, 0.25);\n"
    "  float w = (u_cell_uv.x * u_cell_uv.y) / cosw;\n"
    "  vec3 nrm = (dot(v_nrm, v_nrm) > 1e-8) ? normalize(v_nrm) : (gn / gl2);\n"
    "  vec3 alb = u_alb_tint * ((u_has_alb != 0)\n"
    "           ? texture(u_alb_map, v_uv).rgb : vec3(1.0));\n"
    "  vec3 emi = u_emi_tint * ((u_has_emi != 0)\n"
    "           ? texture(u_emi_map, v_uv).rgb : vec3(1.0));\n"
    "  o_area = w;\n"
    "  o_alb = vec4(alb * w, 1.0);\n"
    "  o_emi = vec4(emi * w, 0.0);\n"
    "  o_nrm = vec4(nrm * w, 0.0);\n"
    "}\n";

static GLuint vox_compile(GLenum type, const char *src)
{
    lm_voxi_gl_t *gl = &lm_voxi_gl;
    GLuint s = gl->CreateShader(type);
    gl->ShaderSource(s, 1, &src, NULL);
    gl->CompileShader(s);
    GLint ok = 0;
    gl->GetShaderiv(s, GLV_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl->GetShaderInfoLog(s, (GLsizei)sizeof log, NULL, log);
        fprintf(stderr, "lm_gpu_voxelize: shader compile failed: %s\n", log);
        gl->DeleteShader(s);
        return 0u;
    }
    return s;
}

bool lm_gpu_voxelize_init(const gl_loader_t *loader)
{
    if (loader == NULL || loader->get_proc_address == NULL) return false;
    if (lm_voxi_ready) return true;
    lm_voxi_gl_t *gl = &lm_voxi_gl;
    memset(gl, 0, sizeof *gl);
#define VX_LOAD(field, name) do {                                              \
    void *p_ = loader->get_proc_address((name), loader->user_data);            \
    if (p_ == NULL) { fprintf(stderr, "lm_gpu_voxelize: missing %s\n", name);  \
                      return false; }                                          \
    memcpy(&gl->field, &p_, sizeof p_); } while (0)
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
    VX_LOAD(Uniform1f, "glUniform1f");
    VX_LOAD(Uniform2f, "glUniform2f");
    VX_LOAD(Uniform3f, "glUniform3f");
    VX_LOAD(Uniform3i, "glUniform3i");
    VX_LOAD(GenBuffers, "glGenBuffers");
    VX_LOAD(DeleteBuffers, "glDeleteBuffers");
    VX_LOAD(BindBuffer, "glBindBuffer");
    VX_LOAD(BufferData, "glBufferData");
    VX_LOAD(BindBufferBase, "glBindBufferBase");
    VX_LOAD(GetBufferSubData, "glGetBufferSubData");
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
    VX_LOAD(CheckFramebufferStatus, "glCheckFramebufferStatus");
    VX_LOAD(FramebufferTextureLayer, "glFramebufferTextureLayer");
    VX_LOAD(DrawBuffers, "glDrawBuffers");
    VX_LOAD(ClearBufferfv, "glClearBufferfv");
    VX_LOAD(BlendEquationi, "glBlendEquationi");
    VX_LOAD(BlendFunc, "glBlendFunc");
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
        if (vs) gl->DeleteShader(vs);
        return false;
    }
    lm_voxi_prog = gl->CreateProgram();
    gl->AttachShader(lm_voxi_prog, vs);
    gl->AttachShader(lm_voxi_prog, fs);
    gl->LinkProgram(lm_voxi_prog);
    gl->DeleteShader(vs);
    gl->DeleteShader(fs);
    GLint ok = 0;
    gl->GetProgramiv(lm_voxi_prog, GLV_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl->GetProgramInfoLog(lm_voxi_prog, (GLsizei)sizeof log, NULL, log);
        fprintf(stderr, "lm_gpu_voxelize: link failed: %s\n", log);
        gl->DeleteProgram(lm_voxi_prog);
        lm_voxi_prog = 0u;
        return false;
    }
    gl->GenFramebuffers(1, &lm_voxi_fbo);
    lm_voxi_ready = true;
    return true;
}

void lm_gpu_voxelize_shutdown(void)
{
    lm_voxi_gl_t *gl = &lm_voxi_gl;
    if (gl->DeleteProgram != NULL) {
        if (lm_voxi_prog)   gl->DeleteProgram(lm_voxi_prog);
        if (lm_voxi_sample) gl->DeleteProgram(lm_voxi_sample);
        if (lm_voxi_fbo && gl->DeleteFramebuffers)
            gl->DeleteFramebuffers(1, &lm_voxi_fbo);
    }
    lm_voxi_prog = lm_voxi_sample = lm_voxi_fbo = 0u;
    lm_voxi_ready = false;
}

/* Composite one axis pass of one window into the accumulating grid arrays.
 * World cell (x,y,z) -> texel (u,v,layer): the two non-axis dims ascending. */
static void vox_composite(lm_gpu_vox_grid_t *g, float *cover, int zbase,
                          int zs, const lm_voxi_vols_t *vols,
                          const float *rb_area, const float *rb_alb,
                          const float *rb_emi, const float *rb_nrm,
                          const float *rb_trans)
{
    const int ax = vols->axis;
    for (int z = 0; z < zs; ++z)
        for (int y = 0; y < g->dims[1]; ++y)
            for (int x = 0; x < g->dims[0]; ++x) {
                int c3[3] = { x, y, z };
                int u = ax == 0 ? c3[1] : c3[0];
                int v = ax == 2 ? c3[1] : c3[2];
                int l = c3[ax];
                size_t t = ((size_t)l * (size_t)vols->vdim + (size_t)v) *
                           (size_t)vols->udim + (size_t)u;
                size_t dst = ((size_t)(zbase + z) * (size_t)g->dims[1] +
                              (size_t)y) * (size_t)g->dims[0] + (size_t)x;
                g->area[dst] += rb_area[t];
                cover[dst] += rb_alb[t * 4 + 3];
                for (int k = 0; k < 3; ++k) {
                    g->albedo[dst * 3 + k]   += rb_alb[t * 4 + k];
                    g->emissive[dst * 3 + k] += rb_emi[t * 4 + k];
                    g->normal[dst * 3 + k]   += rb_nrm[t * 4 + k];
                }
                if (rb_trans[t] < g->trans[dst]) g->trans[dst] = rb_trans[t];
            }
}

bool lm_gpu_voxelize_run(const lm_mesh_t *meshes, uint32_t n_meshes,
                         const phys_aabb_t *box, const int dims[3],
                         lm_gpu_vox_grid_t *out)
{
    lm_voxi_gl_t *gl = &lm_voxi_gl;
    if (!lm_voxi_ready || box == NULL || dims == NULL || out == NULL ||
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
    float *cover = calloc(n, sizeof(float));
    if (!g.occ || !g.area || !g.albedo || !g.emissive || !g.normal ||
        !g.trans || !cover) {
        free(cover);
        lm_gpu_vox_grid_free(&g);
        return false;
    }
    for (size_t i = 0; i < n; ++i) g.trans[i] = 1.0f;

    /* GPU-resident meshes (culled to the box) + material texture dedupe. */
    const float bmin[3] = { box->min.x, box->min.y, box->min.z };
    const float bmax[3] = { box->max.x, box->max.y, box->max.z };
    lm_voxi_scene_t sc;
    if (!lm_voxi_scene_upload(meshes, n_meshes, bmin, bmax, &sc)) {
        free(cover);
        lm_gpu_vox_grid_free(&g);
        return false;
    }
    bool ok = true;

    /* Saved state (the offline bake owns the context; restore what we touch). */
    GLint vp[4];
    gl->GetIntegerv(GLV_VIEWPORT, vp);
    GLboolean depth_on = gl->IsEnabled(GLV_DEPTH_TEST);
    GLboolean cull_on = gl->IsEnabled(GLV_CULL_FACE);

    /* z-slab windows bound GPU memory at full resolution. */
    int zslab = (int)(LM_VOX_BUDGET_CELLS /
                      (size_t)((size_t)dims[0] * (size_t)dims[1]));
    if (zslab < 1) zslab = 1;
    if (zslab > dims[2]) zslab = dims[2];

    size_t max_cells = (size_t)dims[0] * (size_t)dims[1] * (size_t)zslab;
    float *rb_area = malloc(max_cells * sizeof(float));
    float *rb_alb  = malloc(max_cells * 4u * sizeof(float));
    float *rb_emi  = malloc(max_cells * 4u * sizeof(float));
    float *rb_nrm  = malloc(max_cells * 4u * sizeof(float));
    float *rb_tr   = malloc(max_cells * sizeof(float));
    if (!rb_area || !rb_alb || !rb_emi || !rb_nrm || !rb_tr) ok = false;

    for (int zbase = 0; zbase < dims[2] && ok; zbase += zslab) {
        int zs = zslab;
        if (zbase + zs > dims[2]) zs = dims[2] - zbase;
        float worg[3] = { g.origin[0], g.origin[1],
                          g.origin[2] + (float)zbase * g.cell[2] };
        float wext[3] = { ext[0], ext[1], (float)zs * g.cell[2] };
        int wdims[3] = { dims[0], dims[1], zs };
        for (int axis = 0; axis < 3 && ok; ++axis) {
            lm_voxi_vols_t vols;
            if (!lm_voxi_vols_create(&vols, wdims, axis)) { ok = false; break; }
            lm_voxi_raster_window(sc.gm, sc.n_gm, worg, wext, wdims, &vols);
            lm_voxi_vols_read(&vols, LM_VOX_CH_AREA, 1, rb_area);
            lm_voxi_vols_read(&vols, LM_VOX_CH_ALB, 4, rb_alb);
            lm_voxi_vols_read(&vols, LM_VOX_CH_EMI, 4, rb_emi);
            lm_voxi_vols_read(&vols, LM_VOX_CH_NRM, 4, rb_nrm);
            lm_voxi_vols_read(&vols, LM_VOX_CH_TRANS, 1, rb_tr);
            lm_voxi_vols_free(&vols);
            vox_composite(&g, cover, zbase, zs, &vols, rb_area, rb_alb,
                          rb_emi, rb_nrm, rb_tr);
        }
    }
    gl->Finish();

    /* Finalize: occupancy from coverage; area-mean albedo; renormalised
     * normal; emissive over the voxel cross-section. */
    const float vox_area = (g.cell[0] * g.cell[1] + g.cell[1] * g.cell[2] +
                            g.cell[0] * g.cell[2]) / 3.0f;
    const float inv_va = vox_area > 0.0f ? 1.0f / vox_area : 0.0f;
    for (size_t i = 0; i < n && ok; ++i) {
        g.occ[i] = cover[i] > 0.5f ? 1u : 0u;
        if (g.area[i] > 0.0f) {
            float inv = 1.0f / g.area[i];
            for (int k = 0; k < 3; ++k) g.albedo[i * 3 + k] *= inv;
            float *nn = &g.normal[i * 3];
            float nl = sqrtf(nn[0]*nn[0] + nn[1]*nn[1] + nn[2]*nn[2]);
            if (nl > 1e-8f) { nn[0] /= nl; nn[1] /= nl; nn[2] /= nl; }
        } else {
            g.normal[i * 3] = g.normal[i * 3 + 1] = g.normal[i * 3 + 2] = 0.0f;
        }
        for (int k = 0; k < 3; ++k) g.emissive[i * 3 + k] *= inv_va;
    }

    /* teardown + state restore */
    free(rb_area); free(rb_alb); free(rb_emi); free(rb_nrm); free(rb_tr);
    free(cover);
    lm_voxi_scene_free(&sc);
    gl->UseProgram(0u);
    gl->BindFramebuffer(GLV_FRAMEBUFFER, 0u);
    gl->Viewport(vp[0], vp[1], vp[2], vp[3]);
    gl->DepthMask(1);
    if (depth_on) gl->Enable(GLV_DEPTH_TEST);
    if (cull_on) gl->Enable(GLV_CULL_FACE);

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

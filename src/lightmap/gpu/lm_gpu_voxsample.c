/**
 * @file lm_gpu_voxsample.c
 * @brief Full-resolution tiled point sampling of the GPU voxelization (see
 *        lm_gpu_voxelize.h, rpg-bpiz).
 *
 * The dense grid of a deep chunk octree (fine bake voxels over a 64-256 m
 * box) can run to billions of cells -- far beyond any readback budget -- but
 * its CONSUMER only needs the voxelization at known points (SVO leaf
 * centres). So the box is processed in cubic tiles at FULL resolution: each
 * tile is rasterized with the sliced-render-target pipeline (three axis
 * passes, hardware slab clipping) and a small compute pass gathers the
 * channel volumes at the tile's points (texelFetch -- a gather, not
 * rasterization), accumulating into one output buffer. Total readback scales
 * with the point count, never with the grid volume; tiles containing no
 * points are skipped entirely.
 */
#include "ferrum/lightmap/gpu/lm_gpu_voxelize.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lm_gpu_voxelize_internal.h"

/* Gather CS: one thread per tile point; composites this axis pass into the
 * per-point accumulators (area/albedo/emissive add, coverage add, trans min).
 * Out layout per point: 0 area, 1-3 albedo*area, 4-6 emissive*area, 7 cover,
 * 8 trans. */
static const char *const VOXS_CS =
    "#version 430 core\n"
    "layout(local_size_x=64) in;\n"
    "layout(std430, binding=0) buffer P { vec4 pts[]; };\n"
    "layout(std430, binding=1) buffer O { float outv[]; };\n"
    "uniform sampler3D u_area; uniform sampler3D u_alb;\n"
    "uniform sampler3D u_emi; uniform sampler3D u_trans;\n"
    "uniform ivec3 u_perm; uniform ivec3 u_dims;\n"
    "uniform vec3 u_org; uniform vec3 u_cell; uniform int u_count;\n"
    "void main(){\n"
    "  uint i = gl_GlobalInvocationID.x;\n"
    "  if (i >= uint(u_count)) return;\n"
    "  vec3 p = pts[i].xyz;\n"
    "  ivec3 c = clamp(ivec3(floor((p - u_org) / u_cell)),\n"
    "                  ivec3(0), u_dims - 1);\n"
    "  ivec3 t = ivec3(c[u_perm.x], c[u_perm.y], c[u_perm.z]);\n"
    "  vec4 al = texelFetch(u_alb, t, 0);\n"
    "  uint b = uint(pts[i].w) * 9u;\n"
    "  outv[b + 0] += texelFetch(u_area, t, 0).r;\n"
    "  outv[b + 1] += al.r; outv[b + 2] += al.g; outv[b + 3] += al.b;\n"
    "  vec4 em = texelFetch(u_emi, t, 0);\n"
    "  outv[b + 4] += em.r; outv[b + 5] += em.g; outv[b + 6] += em.b;\n"
    "  outv[b + 7] += al.a;\n"
    "  outv[b + 8] = min(outv[b + 8], texelFetch(u_trans, t, 0).r);\n"
    "}\n";

static bool voxs_prog(void)
{
    if (lm_voxi_sample) return true;
    lm_voxi_gl_t *gl = &lm_voxi_gl;
    GLuint s = gl->CreateShader(GLV_COMPUTE_SHADER);
    gl->ShaderSource(s, 1, &VOXS_CS, NULL);
    gl->CompileShader(s);
    GLint ok = 0;
    gl->GetShaderiv(s, GLV_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl->GetShaderInfoLog(s, (GLsizei)sizeof log, NULL, log);
        fprintf(stderr, "lm_gpu_voxsample: compile failed: %s\n", log);
        gl->DeleteShader(s);
        return false;
    }
    lm_voxi_sample = gl->CreateProgram();
    gl->AttachShader(lm_voxi_sample, s);
    gl->LinkProgram(lm_voxi_sample);
    gl->DeleteShader(s);
    gl->GetProgramiv(lm_voxi_sample, GLV_LINK_STATUS, &ok);
    if (!ok) {
        gl->DeleteProgram(lm_voxi_sample);
        lm_voxi_sample = 0u;
        return false;
    }
    return true;
}

bool lm_gpu_voxelize_sample(const lm_mesh_t *meshes, uint32_t n_meshes,
                            const phys_aabb_t *box, const int dims[3],
                            const float *points, uint32_t n_points,
                            float *out_area, float *out_albedo,
                            float *out_emissive)
{
    lm_voxi_gl_t *gl = &lm_voxi_gl;
    if (!lm_voxi_ready || box == NULL || dims == NULL || points == NULL ||
        n_points == 0 || out_area == NULL || out_albedo == NULL ||
        out_emissive == NULL || (meshes == NULL && n_meshes > 0))
        return false;
    if (dims[0] < 1 || dims[1] < 1 || dims[2] < 1) return false;
    const float ext[3] = { box->max.x - box->min.x, box->max.y - box->min.y,
                           box->max.z - box->min.z };
    if (ext[0] <= 0.0f || ext[1] <= 0.0f || ext[2] <= 0.0f) return false;
    if (!voxs_prog()) return false;

    const float org[3] = { box->min.x, box->min.y, box->min.z };
    float cell[3];
    int tiles[3];
    for (int a = 0; a < 3; ++a) {
        cell[a] = ext[a] / (float)dims[a];
        tiles[a] = (dims[a] + LM_VOX_TILE - 1) / LM_VOX_TILE;
    }
    const uint32_t n_tiles =
        (uint32_t)tiles[0] * (uint32_t)tiles[1] * (uint32_t)tiles[2];

    /* Bucket points by tile (count -> offsets -> scatter as vec4 w=index). */
    uint32_t *tile_of = malloc((size_t)n_points * sizeof(uint32_t));
    uint32_t *cnt = calloc((size_t)n_tiles + 1u, sizeof(uint32_t));
    float *scat = malloc((size_t)n_points * 4u * sizeof(float));
    if (!tile_of || !cnt || !scat) {
        free(tile_of); free(cnt); free(scat);
        return false;
    }
    for (uint32_t i = 0; i < n_points; ++i) {
        uint32_t td[3];
        for (int a = 0; a < 3; ++a) {
            float rc = (points[i * 3 + a] - org[a]) / cell[a];
            int ci = (int)rc;
            if (ci < 0) ci = 0;
            if (ci >= dims[a]) ci = dims[a] - 1;
            td[a] = (uint32_t)(ci / LM_VOX_TILE);
        }
        tile_of[i] = (td[2] * (uint32_t)tiles[1] + td[1]) *
                     (uint32_t)tiles[0] + td[0];
        ++cnt[tile_of[i] + 1u];
    }
    for (uint32_t t = 0; t < n_tiles; ++t) cnt[t + 1u] += cnt[t];
    uint32_t *fill = malloc((size_t)n_tiles * sizeof(uint32_t));
    if (fill == NULL) {
        free(tile_of); free(cnt); free(scat);
        return false;
    }
    memcpy(fill, cnt, (size_t)n_tiles * sizeof(uint32_t));
    for (uint32_t i = 0; i < n_points; ++i) {
        uint32_t at = fill[tile_of[i]]++;
        scat[at * 4 + 0] = points[i * 3 + 0];
        scat[at * 4 + 1] = points[i * 3 + 1];
        scat[at * 4 + 2] = points[i * 3 + 2];
        scat[at * 4 + 3] = (float)i;
    }
    free(fill);
    free(tile_of);

    /* GPU-resident meshes (culled to the whole box) + texture dedupe. */
    lm_voxi_mesh_t *gm = calloc(n_meshes ? n_meshes : 1u, sizeof *gm);
    const lm_image_t **imgs = calloc((size_t)n_meshes * 2u + 1u, sizeof *imgs);
    GLuint *img_tex = calloc((size_t)n_meshes * 2u + 1u, sizeof *img_tex);
    float *out9 = malloc((size_t)n_points * 9u * sizeof(float));
    if (gm == NULL || imgs == NULL || img_tex == NULL || out9 == NULL) {
        free(gm); free((void *)imgs); free(img_tex); free(out9);
        free(cnt); free(scat);
        return false;
    }
    for (uint32_t i = 0; i < n_points; ++i) {
        for (int k = 0; k < 8; ++k) out9[i * 9u + (uint32_t)k] = 0.0f;
        out9[i * 9u + 8u] = 1.0f;                        /* trans MIN seed */
    }
    const float bmax9[3] = { box->max.x, box->max.y, box->max.z };
    uint32_t n_gm = 0, n_img = 0;
    bool ok = true;
    for (uint32_t i = 0; i < n_meshes && ok; ++i) {
        const lm_mesh_t *m = &meshes[i];
        if (m->index_count < 3 || m->positions == NULL || m->indices == NULL)
            continue;
        if (!lm_voxi_mesh_overlaps(m, org, bmax9)) continue;
        ok = lm_voxi_upload_mesh(m, &gm[n_gm]);
        if (!ok) break;
        const lm_image_t *want[2] = { m->albedo_image, m->emissive_image };
        GLuint got[2] = { 0u, 0u };
        for (int k = 0; k < 2; ++k) {
            if (want[k] == NULL) continue;
            for (uint32_t j = 0; j < n_img; ++j)
                if (imgs[j] == want[k]) { got[k] = img_tex[j]; break; }
            if (got[k] == 0u) {
                got[k] = lm_voxi_upload_image(want[k]);
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

    GLint vp[4];
    gl->GetIntegerv(GLV_VIEWPORT, vp);
    GLboolean depth_on = gl->IsEnabled(GLV_DEPTH_TEST);
    GLboolean cull_on = gl->IsEnabled(GLV_CULL_FACE);

    GLuint bufs[2] = { 0u, 0u };                       /* [0]=pts [1]=out */
    if (ok) {
        gl->GenBuffers(2, bufs);
        gl->BindBuffer(GLV_SHADER_STORAGE_BUFFER, bufs[1]);
        gl->BufferData(GLV_SHADER_STORAGE_BUFFER,
                       (GLsizeiptr)((size_t)n_points * 9u * sizeof(float)),
                       out9, GLV_DYNAMIC_DRAW);
    }

    for (uint32_t t = 0; t < n_tiles && ok; ++t) {
        uint32_t p0 = cnt[t], p1 = cnt[t + 1u];
        if (p0 == p1)
            continue;                                  /* no points: skip */
        uint32_t tc[3] = { t % (uint32_t)tiles[0],
                           (t / (uint32_t)tiles[0]) % (uint32_t)tiles[1],
                           t / ((uint32_t)tiles[0] * (uint32_t)tiles[1]) };
        int tdims[3];
        float torg[3], text9[3];
        for (int a = 0; a < 3; ++a) {
            int c0 = (int)tc[a] * LM_VOX_TILE;
            tdims[a] = dims[a] - c0 < LM_VOX_TILE ? dims[a] - c0 : LM_VOX_TILE;
            torg[a] = org[a] + (float)c0 * cell[a];
            text9[a] = (float)tdims[a] * cell[a];
        }
        /* geometry crossing the tile? none -> outputs stay zeroed. */
        int any = 0;
        for (uint32_t i = 0; i < n_gm && !any; ++i) {
            any = 1;
            for (int c = 0; c < 3; ++c)
                if (gm[i].bb_max[c] < torg[c] ||
                    gm[i].bb_min[c] > torg[c] + text9[c])
                    any = 0;
        }
        if (!any)
            continue;
        gl->BindBuffer(GLV_SHADER_STORAGE_BUFFER, bufs[0]);
        gl->BufferData(GLV_SHADER_STORAGE_BUFFER,
                       (GLsizeiptr)((size_t)(p1 - p0) * 4u * sizeof(float)),
                       &scat[(size_t)p0 * 4u], GLV_DYNAMIC_DRAW);
        for (int axis = 0; axis < 3 && ok; ++axis) {
            lm_voxi_vols_t vols;
            if (!lm_voxi_vols_create(&vols, tdims, axis)) { ok = false; break; }
            lm_voxi_raster_window(gm, n_gm, torg, text9, tdims, &vols);
            /* gather this axis pass at the tile's points */
            gl->UseProgram(lm_voxi_sample);
            static const int perm[3][3] = { {1,2,0}, {0,2,1}, {0,1,2} };
            gl->Uniform3i(gl->GetUniformLocation(lm_voxi_sample, "u_perm"),
                          perm[axis][0], perm[axis][1], perm[axis][2]);
            gl->Uniform3i(gl->GetUniformLocation(lm_voxi_sample, "u_dims"),
                          tdims[0], tdims[1], tdims[2]);
            gl->Uniform3f(gl->GetUniformLocation(lm_voxi_sample, "u_org"),
                          torg[0], torg[1], torg[2]);
            gl->Uniform3f(gl->GetUniformLocation(lm_voxi_sample, "u_cell"),
                          cell[0], cell[1], cell[2]);
            gl->Uniform1i(gl->GetUniformLocation(lm_voxi_sample, "u_count"),
                          (GLint)(p1 - p0));
            static const char *const su[4] = { "u_area", "u_alb", "u_emi",
                                               "u_trans" };
            static const int sc[4] = { LM_VOX_CH_AREA, LM_VOX_CH_ALB,
                                       LM_VOX_CH_EMI, LM_VOX_CH_TRANS };
            for (int k = 0; k < 4; ++k) {
                gl->ActiveTexture(GLV_TEXTURE0 + 3 + (GLenum)k);
                gl->BindTexture(GLV_TEXTURE_3D, vols.tex[sc[k]]);
                gl->Uniform1i(gl->GetUniformLocation(lm_voxi_sample, su[k]),
                              3 + k);
            }
            gl->ActiveTexture(GLV_TEXTURE0);
            gl->BindBufferBase(GLV_SHADER_STORAGE_BUFFER, 0, bufs[0]);
            gl->BindBufferBase(GLV_SHADER_STORAGE_BUFFER, 1, bufs[1]);
            gl->DispatchCompute((GLuint)((p1 - p0 + 63u) / 64u), 1u, 1u);
            gl->MemoryBarrier(GLV_ALL_BARRIER_BITS);
            lm_voxi_vols_free(&vols);
        }
    }
    if (ok) {
        gl->BindBuffer(GLV_SHADER_STORAGE_BUFFER, bufs[1]);
        gl->GetBufferSubData(GLV_SHADER_STORAGE_BUFFER, 0,
                             (GLsizeiptr)((size_t)n_points * 9u * sizeof(float)),
                             out9);
    }
    gl->Finish();

    if (ok) {
        for (uint32_t i = 0; i < n_points; ++i) {
            const float *v = &out9[(size_t)i * 9u];
            out_area[i] = v[0];
            float inv = v[0] > 0.0f ? 1.0f / v[0] : 0.0f;
            for (int k = 0; k < 3; ++k) {
                out_albedo[i * 3 + (uint32_t)k] = v[1 + k] * inv;
                out_emissive[i * 3 + (uint32_t)k] = v[4 + k];
            }
        }
        /* emissive: per voxel cross-section (matches the dense decode). */
        float vox_area = (cell[0] * cell[1] + cell[1] * cell[2] +
                          cell[0] * cell[2]) / 3.0f;
        if (vox_area > 0.0f)
            for (uint32_t i = 0; i < n_points * 3u; ++i)
                out_emissive[i] /= vox_area;
    }

    if (bufs[0] || bufs[1]) gl->DeleteBuffers(2, bufs);
    for (uint32_t i = 0; i < n_gm; ++i) lm_voxi_free_mesh(&gm[i]);
    for (uint32_t j = 0; j < n_img; ++j)
        if (img_tex[j]) gl->DeleteTextures(1, &img_tex[j]);
    free(gm); free((void *)imgs); free(img_tex);
    free(out9); free(cnt); free(scat);
    gl->UseProgram(0u);
    gl->BindFramebuffer(GLV_FRAMEBUFFER, 0u);
    gl->Viewport(vp[0], vp[1], vp[2], vp[3]);
    gl->DepthMask(1);
    if (depth_on) gl->Enable(GLV_DEPTH_TEST);
    if (cull_on) gl->Enable(GLV_CULL_FACE);
    return ok;
}

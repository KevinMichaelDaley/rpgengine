/**
 * @file lm_gpu_chunk_build.c
 * @brief GPU chunk-SVO build (see lm_gpu_chunk_build.h, rpg-bpiz).
 */
#include "ferrum/lightmap/gpu/lm_gpu_chunk_build.h"

#include <stdio.h>
#include <stdlib.h>

#include "ferrum/lightmap/lm_svo_mip.h"
#include "lm_gpu_voxelize_internal.h"

#define LM_CB_REC_FLOATS 10u   /* cell xyz, area, albedo sum rgb, emissive rgb */

/* Compact CS: compose the three axis passes per cell and append one sparse
 * record per COVERED cell. The whole SVO leaf set leaves the GPU this way --
 * readback scales with leaf count, never the dense volume. */
static const char *const CB_CS =
    "#version 430 core\n"
    "layout(local_size_x=4, local_size_y=4, local_size_z=4) in;\n"
    "layout(std430, binding=0) buffer C { uint count; };\n"
    "layout(std430, binding=1) buffer R { float rec[]; };\n"
    "uniform sampler3D u_area0; uniform sampler3D u_alb0; uniform sampler3D u_emi0;\n"
    "uniform sampler3D u_area1; uniform sampler3D u_alb1; uniform sampler3D u_emi1;\n"
    "uniform sampler3D u_area2; uniform sampler3D u_alb2; uniform sampler3D u_emi2;\n"
    "uniform ivec3 u_dims; uniform ivec3 u_cell0; uniform int u_max;\n"
    "void main(){\n"
    "  ivec3 c = ivec3(gl_GlobalInvocationID);\n"
    "  if (any(greaterThanEqual(c, u_dims))) return;\n"
    "  ivec3 t0 = ivec3(c.y, c.z, c.x);\n"
    "  ivec3 t1 = ivec3(c.x, c.z, c.y);\n"
    "  vec4 al = texelFetch(u_alb0, t0, 0) + texelFetch(u_alb1, t1, 0)\n"
    "          + texelFetch(u_alb2, c, 0);\n"
    "  if (al.a <= 0.0) return;\n"
    "  float A = texelFetch(u_area0, t0, 0).r + texelFetch(u_area1, t1, 0).r\n"
    "          + texelFetch(u_area2, c, 0).r;\n"
    "  vec4 em = texelFetch(u_emi0, t0, 0) + texelFetch(u_emi1, t1, 0)\n"
    "          + texelFetch(u_emi2, c, 0);\n"
    "  uint at = atomicAdd(count, 1u);\n"
    "  if (at >= uint(u_max)) return;\n"
    "  uint b = at * 10u;\n"
    "  rec[b+0] = float(u_cell0.x + c.x);\n"
    "  rec[b+1] = float(u_cell0.y + c.y);\n"
    "  rec[b+2] = float(u_cell0.z + c.z);\n"
    "  rec[b+3] = A;\n"
    "  rec[b+4] = al.r; rec[b+5] = al.g; rec[b+6] = al.b;\n"
    "  rec[b+7] = em.r; rec[b+8] = em.g; rec[b+9] = em.b;\n"
    "}\n";

static GLuint cb_prog;

static bool cb_ensure_prog(void)
{
    if (cb_prog) return true;
    lm_voxi_gl_t *gl = &lm_voxi_gl;
    GLuint s = gl->CreateShader(GLV_COMPUTE_SHADER);
    gl->ShaderSource(s, 1, &CB_CS, NULL);
    gl->CompileShader(s);
    GLint ok = 0;
    gl->GetShaderiv(s, GLV_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl->GetShaderInfoLog(s, (GLsizei)sizeof log, NULL, log);
        fprintf(stderr, "lm_gpu_chunk_build: compile failed: %s\n", log);
        gl->DeleteShader(s);
        return false;
    }
    cb_prog = gl->CreateProgram();
    gl->AttachShader(cb_prog, s);
    gl->LinkProgram(cb_prog);
    gl->DeleteShader(s);
    gl->GetProgramiv(cb_prog, GLV_LINK_STATUS, &ok);
    if (!ok) {
        gl->DeleteProgram(cb_prog);
        cb_prog = 0u;
        return false;
    }
    return true;
}

/* Depth whose voxel edge is closest to @p voxel (must MATCH the CPU
 * lm_chunk_svo_build derivation so GPU/CPU chunks are interchangeable). */
static uint32_t cb_depth(phys_aabb_t box, float voxel)
{
    float ex = box.max.x - box.min.x, ey = box.max.y - box.min.y,
          ez = box.max.z - box.min.z;
    float ext = ex > ey ? (ex > ez ? ex : ez) : (ey > ez ? ey : ez);
    if (voxel <= 0.0f || ext <= 0.0f) return NPC_SVO_MAX_DEPTH;
    uint32_t d = 1;
    while (d < NPC_SVO_MAX_DEPTH && ext / (float)(1u << d) > voxel) ++d;
    if (d > 1) {
        float coarse = ext / (float)(1u << (d - 1)), fine = ext / (float)(1u << d);
        if (coarse - voxel < voxel - fine) --d;
    }
    return d;
}

/* Descend-insert one leaf (the npc rasterizer's ensure_leaf_ walk via the
 * public pool API: interior nodes + occupancy masks materialize on the way). */
static uint32_t cb_insert_leaf(npc_svo_grid_t *grid, uint32_t vx, uint32_t vy,
                               uint32_t vz)
{
    uint32_t node_idx = 0;
    uint32_t cells = 1u << grid->max_depth;
    for (uint32_t d = 0; d < grid->max_depth; ++d) {
        cells >>= 1;
        uint32_t ci = (((vz / cells) & 1u) << 2) | (((vy / cells) & 1u) << 1) |
                      ((vx / cells) & 1u);
        uint32_t child = grid->nodes[node_idx].children[ci];
        if (child == NPC_SVO_INVALID_NODE) {
            child = npc_svo_alloc_node(grid);
            if (child == NPC_SVO_INVALID_NODE)
                return NPC_SVO_INVALID_NODE;
            /* alloc may realloc the pool: index nodes fresh, never cache. */
            grid->nodes[node_idx].children[ci] = child;
            grid->nodes[child].parent = node_idx;
            grid->nodes[node_idx].occupancy |= (uint8_t)(1u << ci);
        }
        node_idx = child;
    }
    return node_idx;
}

bool lm_gpu_chunk_svo_build(const lm_mesh_scene_t *scene, phys_aabb_t box,
                            float voxel, npc_svo_grid_t *out_svo)
{
    lm_voxi_gl_t *gl = &lm_voxi_gl;
    if (!lm_voxi_ready || scene == NULL || out_svo == NULL) return false;
    if (!cb_ensure_prog()) return false;

    uint32_t depth = cb_depth(box, voxel);
    if (!npc_svo_grid_init(out_svo, box, depth)) return false;
    const int cells_axis = (int)(1u << depth);
    const float org[3] = { box.min.x, box.min.y, box.min.z };
    const float bmax[3] = { box.max.x, box.max.y, box.max.z };
    float cell[3];
    for (int a = 0; a < 3; ++a)
        cell[a] = (bmax[a] - org[a]) / (float)cells_axis;
    const float vox_area = (cell[0] * cell[1] + cell[1] * cell[2] +
                            cell[0] * cell[2]) / 3.0f;

    lm_voxi_scene_t sc;
    if (!lm_voxi_scene_upload(scene->meshes, scene->n_meshes, org, bmax, &sc)) {
        npc_svo_grid_destroy(out_svo);
        return false;
    }

    GLint vp[4];
    gl->GetIntegerv(GLV_VIEWPORT, vp);
    GLboolean depth_on = gl->IsEnabled(GLV_DEPTH_TEST);
    GLboolean cull_on = gl->IsEnabled(GLV_CULL_FACE);

    /* Record capacity: a full tile of covered cells (defensive worst case). */
    const uint32_t max_rec = (uint32_t)LM_VOX_TILE * LM_VOX_TILE * LM_VOX_TILE;
    GLuint bufs[2];
    gl->GenBuffers(2, bufs);
    gl->BindBuffer(GLV_SHADER_STORAGE_BUFFER, bufs[1]);
    gl->BufferData(GLV_SHADER_STORAGE_BUFFER,
                   (GLsizeiptr)((size_t)max_rec * LM_CB_REC_FLOATS *
                                sizeof(float)), NULL, GLV_DYNAMIC_DRAW);
    float *rec = malloc((size_t)max_rec * LM_CB_REC_FLOATS * sizeof(float));
    bool ok = rec != NULL;

    const int tiles = (cells_axis + LM_VOX_TILE - 1) / LM_VOX_TILE;
    uint32_t solid_leaves = 0, solid_no_mat = 0, dropped = 0;
    for (int tz = 0; tz < tiles && ok; ++tz)
    for (int ty = 0; ty < tiles && ok; ++ty)
    for (int tx = 0; tx < tiles && ok; ++tx) {
        int c0[3] = { tx * LM_VOX_TILE, ty * LM_VOX_TILE, tz * LM_VOX_TILE };
        int tdims[3];
        float torg[3], text9[3];
        for (int a = 0; a < 3; ++a) {
            int rem = cells_axis - c0[a];
            tdims[a] = rem < LM_VOX_TILE ? rem : LM_VOX_TILE;
            torg[a] = org[a] + (float)c0[a] * cell[a];
            text9[a] = (float)tdims[a] * cell[a];
        }
        int any = 0;
        for (uint32_t i = 0; i < sc.n_gm && !any; ++i) {
            any = 1;
            for (int c = 0; c < 3; ++c)
                if (sc.gm[i].bb_max[c] < torg[c] ||
                    sc.gm[i].bb_min[c] > torg[c] + text9[c])
                    any = 0;
        }
        if (!any)
            continue;                      /* no geometry crosses this tile */

        /* three axis passes, volumes kept alive for the compose+compact */
        lm_voxi_vols_t vols[3];
        int nv = 0;
        for (int axis = 0; axis < 3 && ok; ++axis) {
            if (!lm_voxi_vols_create(&vols[axis], tdims, axis)) { ok = false; break; }
            ++nv;
            lm_voxi_raster_window(sc.gm, sc.n_gm, torg, text9, tdims,
                                  &vols[axis]);
        }
        if (ok) {
            uint32_t zero = 0;
            gl->BindBuffer(GLV_SHADER_STORAGE_BUFFER, bufs[0]);
            gl->BufferData(GLV_SHADER_STORAGE_BUFFER, (GLsizeiptr)sizeof zero,
                           &zero, GLV_DYNAMIC_DRAW);
            gl->UseProgram(cb_prog);
            static const char *const su[3][3] = {
                { "u_area0", "u_alb0", "u_emi0" },
                { "u_area1", "u_alb1", "u_emi1" },
                { "u_area2", "u_alb2", "u_emi2" }
            };
            static const int sch[3] = { LM_VOX_CH_AREA, LM_VOX_CH_ALB,
                                        LM_VOX_CH_EMI };
            for (int axis = 0; axis < 3; ++axis)
                for (int k = 0; k < 3; ++k) {
                    int unit = 3 + axis * 3 + k;
                    gl->ActiveTexture(GLV_TEXTURE0 + (GLenum)unit);
                    gl->BindTexture(GLV_TEXTURE_3D, vols[axis].tex[sch[k]]);
                    gl->Uniform1i(gl->GetUniformLocation(cb_prog, su[axis][k]),
                                  unit);
                }
            gl->ActiveTexture(GLV_TEXTURE0);
            gl->Uniform3i(gl->GetUniformLocation(cb_prog, "u_dims"),
                          tdims[0], tdims[1], tdims[2]);
            gl->Uniform3i(gl->GetUniformLocation(cb_prog, "u_cell0"),
                          c0[0], c0[1], c0[2]);
            gl->Uniform1i(gl->GetUniformLocation(cb_prog, "u_max"),
                          (GLint)max_rec);
            gl->BindBufferBase(GLV_SHADER_STORAGE_BUFFER, 0, bufs[0]);
            gl->BindBufferBase(GLV_SHADER_STORAGE_BUFFER, 1, bufs[1]);
            gl->DispatchCompute((GLuint)((tdims[0] + 3) / 4),
                                (GLuint)((tdims[1] + 3) / 4),
                                (GLuint)((tdims[2] + 3) / 4));
            gl->MemoryBarrier(GLV_ALL_BARRIER_BITS);
            uint32_t cnt = 0;
            gl->BindBuffer(GLV_SHADER_STORAGE_BUFFER, bufs[0]);
            gl->GetBufferSubData(GLV_SHADER_STORAGE_BUFFER, 0,
                                 (GLsizeiptr)sizeof cnt, &cnt);
            if (cnt > max_rec) {
                dropped += cnt - max_rec;
                cnt = max_rec;
            }
            if (cnt > 0) {
                gl->BindBuffer(GLV_SHADER_STORAGE_BUFFER, bufs[1]);
                gl->GetBufferSubData(GLV_SHADER_STORAGE_BUFFER, 0,
                                     (GLsizeiptr)((size_t)cnt *
                                                  LM_CB_REC_FLOATS *
                                                  sizeof(float)), rec);
                for (uint32_t r = 0; r < cnt && ok; ++r) {
                    const float *v = &rec[(size_t)r * LM_CB_REC_FLOATS];
                    uint32_t leaf = cb_insert_leaf(out_svo, (uint32_t)v[0],
                                                   (uint32_t)v[1],
                                                   (uint32_t)v[2]);
                    if (leaf == NPC_SVO_INVALID_NODE) { ok = false; break; }
                    npc_svo_node_t *nd = &out_svo->nodes[leaf];
                    nd->flags |= NPC_SVO_FLAG_SOLID;
                    ++solid_leaves;
                    if (v[3] > 0.0f) {
                        float inv = 1.0f / v[3];
                        nd->diffuse[0] = v[4] * inv;
                        nd->diffuse[1] = v[5] * inv;
                        nd->diffuse[2] = v[6] * inv;
                    } else {
                        /* covered but never area-sampled: neutral bounce. */
                        nd->diffuse[0] = nd->diffuse[1] = nd->diffuse[2] = 0.5f;
                        ++solid_no_mat;
                    }
                    float iva = vox_area > 0.0f ? 1.0f / vox_area : 0.0f;
                    nd->emissive[0] = v[7] * iva;
                    nd->emissive[1] = v[8] * iva;
                    nd->emissive[2] = v[9] * iva;
                }
            }
        }
        for (int axis = 0; axis < nv; ++axis) lm_voxi_vols_free(&vols[axis]);
    }
    gl->Finish();

    free(rec);
    gl->DeleteBuffers(2, bufs);
    lm_voxi_scene_free(&sc);
    gl->UseProgram(0u);
    gl->BindFramebuffer(GLV_FRAMEBUFFER, 0u);
    gl->Viewport(vp[0], vp[1], vp[2], vp[3]);
    gl->DepthMask(1);
    if (depth_on) gl->Enable(GLV_DEPTH_TEST);
    if (cull_on) gl->Enable(GLV_CULL_FACE);

    if (!ok || dropped > 0) {
        if (dropped > 0)
            fprintf(stderr, "lm_gpu_chunk_build: %u records dropped "
                            "(tile overflow), falling back\n", dropped);
        npc_svo_grid_destroy(out_svo);
        return false;
    }
    fprintf(stderr,
            "voxelize(gpu): %u solid leaves, %u with NO material (%.1f%% gap)\n",
            solid_leaves, solid_no_mat,
            solid_leaves ? 100.0f * (float)solid_no_mat / (float)solid_leaves
                         : 0.0f);
    lm_svo_mip_average_up(out_svo);
    return true;
}

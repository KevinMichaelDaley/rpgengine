/**
 * @file lm_gpu_voxelize_tests.c
 * @brief Tests for the bake-side GPU voxel rasterizer (rpg-bpiz).
 *
 * Needs a live GL 4.3 context: acquired via the headless EGL harness. When no
 * context can be created (no GPU / no EGL), the whole battery SKIPs with exit
 * code 0 so the suite stays runnable on headless CI.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/lightmap/gpu/lm_gpu_chunk_build.h"
#include "ferrum/lightmap/gpu/lm_gpu_voxelize.h"
#include "ferrum/lightmap/lm_chunk_svo.h"
#include "ferrum/renderer/egl_headless.h"

static int g_checks, g_fails;
#define CHECK(cond, msg) do {                                              \
    ++g_checks;                                                            \
    if (!(cond)) { ++g_fails; printf("FAIL %s:%d %s\n", __func__, __LINE__, msg); } \
} while (0)

/* ── tiny scene builders (static storage: lm_mesh_t borrows pointers) ── */

/* Axis-aligned box shell [lo,hi]^3: 24 verts (per-face normals), 36 indices. */
static float    s_box_pos[24 * 3], s_box_nrm[24 * 3], s_box_uv[24 * 2];
static uint32_t s_box_idx[36];

static void build_box(float lo, float hi)
{
    static const float fn[6][3] = { {-1,0,0}, {1,0,0}, {0,-1,0}, {0,1,0},
                                    {0,0,-1}, {0,0,1} };
    /* Per face: axis = non-zero component, the other two sweep the quad. */
    int vi = 0, ii = 0;
    for (int f = 0; f < 6; ++f) {
        int ax = fn[f][0] != 0.0f ? 0 : (fn[f][1] != 0.0f ? 1 : 2);
        int u = (ax + 1) % 3, v = (ax + 2) % 3;
        float plane = fn[f][ax] < 0.0f ? lo : hi;
        for (int c = 0; c < 4; ++c) {
            float pu = (c == 1 || c == 2) ? hi : lo;
            float pv = (c >= 2) ? hi : lo;
            s_box_pos[(vi + c) * 3 + ax] = plane;
            s_box_pos[(vi + c) * 3 + u]  = pu;
            s_box_pos[(vi + c) * 3 + v]  = pv;
            for (int k = 0; k < 3; ++k) s_box_nrm[(vi + c) * 3 + k] = fn[f][k];
            s_box_uv[(vi + c) * 2 + 0] = (c == 1 || c == 2) ? 1.0f : 0.0f;
            s_box_uv[(vi + c) * 2 + 1] = (c >= 2) ? 1.0f : 0.0f;
        }
        s_box_idx[ii + 0] = (uint32_t)vi;     s_box_idx[ii + 1] = (uint32_t)vi + 1;
        s_box_idx[ii + 2] = (uint32_t)vi + 2; s_box_idx[ii + 3] = (uint32_t)vi;
        s_box_idx[ii + 4] = (uint32_t)vi + 2; s_box_idx[ii + 5] = (uint32_t)vi + 3;
        vi += 4; ii += 6;
    }
}

/* One quad in the z = @p z plane spanning [x0,x1]x[y0,y1], uv spanning [0,1]. */
static float    s_quad_pos[4 * 3], s_quad_nrm[4 * 3], s_quad_uv[4 * 2];
static uint32_t s_quad_idx[6] = { 0, 1, 2, 0, 2, 3 };

static void build_quad(float x0, float x1, float y0, float y1, float z)
{
    const float px[4] = { x0, x1, x1, x0 }, py[4] = { y0, y0, y1, y1 };
    const float tu[4] = { 0, 1, 1, 0 },     tv[4] = { 0, 0, 1, 1 };
    for (int c = 0; c < 4; ++c) {
        s_quad_pos[c * 3 + 0] = px[c]; s_quad_pos[c * 3 + 1] = py[c];
        s_quad_pos[c * 3 + 2] = z;
        s_quad_nrm[c * 3 + 0] = 0.0f; s_quad_nrm[c * 3 + 1] = 0.0f;
        s_quad_nrm[c * 3 + 2] = 1.0f;
        s_quad_uv[c * 2 + 0] = tu[c]; s_quad_uv[c * 2 + 1] = tv[c];
    }
}

static lm_mesh_t mesh_of(const float *pos, const float *nrm, const float *uv,
                         uint32_t nv, const uint32_t *idx, uint32_t ni,
                         float ar, float ag, float ab, float opacity)
{
    lm_mesh_t m;
    memset(&m, 0, sizeof m);
    m.positions = pos; m.normals = nrm; m.uv0 = uv;
    m.vert_count = nv; m.indices = idx; m.index_count = ni;
    m.albedo   = (vec3_t){ ar, ag, ab };
    m.emissive = (vec3_t){ 0.0f, 0.0f, 0.0f };
    m.opacity  = opacity;
    return m;
}

static size_t cell(const lm_gpu_vox_grid_t *g, int x, int y, int z)
{
    return ((size_t)z * (size_t)g->dims[1] + (size_t)y) * (size_t)g->dims[0]
           + (size_t)x;
}

/* ── the battery ── */

static void t_fail_args(void)
{
    lm_gpu_vox_grid_t g;
    phys_aabb_t box = { { 0, 0, 0 }, { 1, 1, 1 } };
    int dims[3] = { 4, 4, 4 };
    int bad[3] = { 0, 4, 4 };
    lm_mesh_t m = mesh_of(s_quad_pos, s_quad_nrm, s_quad_uv, 4, s_quad_idx, 6,
                          1, 1, 1, 1.0f);
    CHECK(!lm_gpu_voxelize_run(&m, 1, NULL, dims, &g), "NULL box accepted");
    CHECK(!lm_gpu_voxelize_run(&m, 1, &box, NULL, &g), "NULL dims accepted");
    CHECK(!lm_gpu_voxelize_run(&m, 1, &box, bad, &g), "dims<1 accepted");
    CHECK(!lm_gpu_voxelize_run(&m, 1, &box, dims, NULL), "NULL out accepted");
    CHECK(!lm_gpu_voxelize_run(NULL, 1, &box, dims, &g), "NULL meshes + n>0 accepted");
}

static void t_empty_scene(void)
{
    lm_gpu_vox_grid_t g;
    phys_aabb_t box = { { 0, 0, 0 }, { 1, 1, 1 } };
    int dims[3] = { 2, 2, 2 };
    CHECK(lm_gpu_voxelize_run(NULL, 0, &box, dims, &g), "empty scene failed");
    size_t n = 8;
    int any = 0;
    for (size_t i = 0; i < n; ++i) any |= (int)g.occ[i];
    CHECK(!any, "empty scene produced occupancy");
    for (size_t i = 0; i < n; ++i)
        CHECK(fabsf(g.trans[i] - 1.0f) < 1e-3f, "empty cell not clear");
    lm_gpu_vox_grid_free(&g);
}

static void t_cube(void)
{
    build_box(0.6f, 2.4f);
    lm_mesh_t m = mesh_of(s_box_pos, s_box_nrm, s_box_uv, 24, s_box_idx, 36,
                          0.8f, 0.2f, 0.1f, 1.0f);
    lm_gpu_vox_grid_t g;
    phys_aabb_t box = { { -0.5f, -0.5f, -0.5f }, { 3.5f, 3.5f, 3.5f } };
    int dims[3] = { 8, 8, 8 };                    /* 0.5 m cells */
    CHECK(lm_gpu_voxelize_run(&m, 1, &box, dims, &g), "cube run failed");

    /* -x face centre (0.6,1.5,1.5) -> cell (2,4,4); interior (1.5,..) -> (4,4,4). */
    CHECK(g.occ[cell(&g, 2, 4, 4)] != 0, "-x face cell empty");
    CHECK(g.occ[cell(&g, 4, 4, 4)] == 0, "interior cell solid");
    uint32_t solid = 0;
    for (int i = 0; i < 8 * 8 * 8; ++i) solid += g.occ[i] ? 1u : 0u;
    CHECK(solid >= 50 && solid <= 260, "cube shell cell count out of range");

    size_t f = cell(&g, 2, 4, 4);
    CHECK(g.area[f] > 0.0f, "face cell has no accumulated area");
    CHECK(fabsf(g.albedo[f * 3 + 0] - 0.8f) < 0.08f &&
          fabsf(g.albedo[f * 3 + 1] - 0.2f) < 0.08f &&
          fabsf(g.albedo[f * 3 + 2] - 0.1f) < 0.08f, "face albedo != tint");
    CHECK(g.normal[f * 3 + 0] < -0.9f && fabsf(g.normal[f * 3 + 1]) < 0.25f &&
          fabsf(g.normal[f * 3 + 2]) < 0.25f, "-x face normal wrong");
    CHECK(g.trans[f] < 0.01f, "opaque face not opaque");
    lm_gpu_vox_grid_free(&g);
}

static void t_edge_on_plane(void)
{
    build_quad(0.0f, 4.0f, 0.0f, 4.0f, 1.03f);
    lm_mesh_t m = mesh_of(s_quad_pos, s_quad_nrm, s_quad_uv, 4, s_quad_idx, 6,
                          0.5f, 0.5f, 0.5f, 1.0f);
    lm_gpu_vox_grid_t g;
    phys_aabb_t box = { { 0, 0, 0 }, { 4, 4, 4 } };
    int dims[3] = { 8, 8, 8 };
    CHECK(lm_gpu_voxelize_run(&m, 1, &box, dims, &g), "plane run failed");
    int all = 1;                        /* z = 1.03 -> k = 2 sheet, all 64 cells */
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            if (!g.occ[cell(&g, x, y, 2)]) all = 0;
    CHECK(all, "edge-on plane sheet has holes");
    lm_gpu_vox_grid_free(&g);
}

static void t_textured_albedo(void)
{
    static const uint8_t pix[6] = { 255, 0, 0,   0, 0, 255 };  /* red | blue */
    lm_image_t img = { pix, 2, 1, 3, false };
    build_quad(0.0f, 2.0f, 0.0f, 1.0f, 0.5f);
    lm_mesh_t m = mesh_of(s_quad_pos, s_quad_nrm, s_quad_uv, 4, s_quad_idx, 6,
                          1.0f, 1.0f, 1.0f, 1.0f);
    m.albedo_image = &img;
    lm_gpu_vox_grid_t g;
    phys_aabb_t box = { { 0, 0, 0 }, { 2, 1, 1 } };
    int dims[3] = { 2, 1, 1 };
    CHECK(lm_gpu_voxelize_run(&m, 1, &box, dims, &g), "textured run failed");
    size_t l = cell(&g, 0, 0, 0), r = cell(&g, 1, 0, 0);
    CHECK(g.occ[l] && g.occ[r], "textured quad cells empty");
    CHECK(g.albedo[l * 3 + 0] > 0.55f && g.albedo[l * 3 + 2] < 0.45f,
          "left cell not red-dominant");
    CHECK(g.albedo[r * 3 + 2] > 0.55f && g.albedo[r * 3 + 0] < 0.45f,
          "right cell not blue-dominant");
    lm_gpu_vox_grid_free(&g);
}

static void t_emissive_sum(void)
{
    build_quad(0.0f, 1.0f, 0.0f, 1.0f, 0.5f);
    lm_mesh_t m = mesh_of(s_quad_pos, s_quad_nrm, s_quad_uv, 4, s_quad_idx, 6,
                          1.0f, 1.0f, 1.0f, 1.0f);
    m.emissive = (vec3_t){ 2.0f, 1.0f, 0.0f };
    lm_gpu_vox_grid_t g;
    phys_aabb_t box = { { 0, 0, 0 }, { 1, 1, 1 } };
    int dims[3] = { 1, 1, 1 };                        /* quad == cross-section */
    CHECK(lm_gpu_voxelize_run(&m, 1, &box, dims, &g), "emissive run failed");
    CHECK(g.occ[0] != 0, "emissive cell empty");
    CHECK(g.emissive[0] > 1.2f && g.emissive[0] < 2.8f, "emissive R off");
    CHECK(g.emissive[1] > 0.6f && g.emissive[1] < 1.4f, "emissive G off");
    CHECK(g.emissive[2] < 0.1f, "emissive B nonzero");
    lm_gpu_vox_grid_free(&g);
}

static void t_transmission(void)
{
    /* Glass quad over BOTH cells; an opaque quad shares only cell 0. */
    static float    gp[4 * 3], gn[4 * 3], gu[4 * 2];
    static uint32_t gi2[6] = { 0, 1, 2, 0, 2, 3 };
    build_quad(0.0f, 2.0f, 0.0f, 1.0f, 0.45f);
    memcpy(gp, s_quad_pos, sizeof gp); memcpy(gn, s_quad_nrm, sizeof gn);
    memcpy(gu, s_quad_uv, sizeof gu);
    lm_mesh_t glass = mesh_of(gp, gn, gu, 4, gi2, 6, 1, 1, 1, 0.35f);
    build_quad(0.0f, 1.0f, 0.0f, 1.0f, 0.55f);
    lm_mesh_t opaque = mesh_of(s_quad_pos, s_quad_nrm, s_quad_uv, 4,
                               s_quad_idx, 6, 1, 1, 1, 1.0f);
    lm_mesh_t ms[2] = { glass, opaque };
    lm_gpu_vox_grid_t g;
    phys_aabb_t box = { { 0, 0, 0 }, { 2, 1, 1 } };
    int dims[3] = { 2, 1, 1 };
    CHECK(lm_gpu_voxelize_run(ms, 2, &box, dims, &g), "transmission run failed");
    CHECK(g.trans[cell(&g, 0, 0, 0)] < 0.01f, "shared cell not opaque (MIN)");
    CHECK(fabsf(g.trans[cell(&g, 1, 0, 0)] - 0.65f) < 0.03f,
          "glass cell transmission != 1-opacity");
    lm_gpu_vox_grid_free(&g);
}

static void t_sample_points(void)
{
    build_box(0.6f, 2.4f);
    lm_mesh_t m = mesh_of(s_box_pos, s_box_nrm, s_box_uv, 24, s_box_idx, 36,
                          0.8f, 0.2f, 0.1f, 1.0f);
    phys_aabb_t box = { { -0.5f, -0.5f, -0.5f }, { 3.5f, 3.5f, 3.5f } };
    int dims[3] = { 8, 8, 8 };
    /* face centre / cube interior / empty space */
    float pts[9] = { 0.6f, 1.5f, 1.5f,  1.5f, 1.5f, 1.5f,  3.4f, 3.4f, 3.4f };
    float area[3], alb[9], emi[9];
    CHECK(!lm_gpu_voxelize_sample(&m, 1, &box, dims, NULL, 3, area, alb, emi),
          "NULL points accepted");
    CHECK(!lm_gpu_voxelize_sample(&m, 1, &box, dims, pts, 0, area, alb, emi),
          "0 points accepted");
    CHECK(lm_gpu_voxelize_sample(&m, 1, &box, dims, pts, 3, area, alb, emi),
          "sample run failed");
    CHECK(area[0] > 0.0f, "face point has no area");
    CHECK(fabsf(alb[0] - 0.8f) < 0.08f && fabsf(alb[1] - 0.2f) < 0.08f &&
          fabsf(alb[2] - 0.1f) < 0.08f, "face point albedo != tint");
    CHECK(area[1] <= 0.0f, "interior point has surface area");
    CHECK(area[2] <= 0.0f, "empty-space point has surface area");
    CHECK(emi[0] < 0.05f, "non-emissive face emits");
}

static void t_sample_tiled(void)
{
    /* dims 256 > LM_VOX_TILE forces a 2x2x2 tile grid at full resolution. */
    build_quad(1.0f, 63.0f, 1.0f, 63.0f, 10.1f);
    lm_mesh_t m = mesh_of(s_quad_pos, s_quad_nrm, s_quad_uv, 4, s_quad_idx, 6,
                          0.3f, 0.9f, 0.2f, 1.0f);
    phys_aabb_t box = { { 0, 0, 0 }, { 64, 64, 64 } };
    int dims[3] = { 256, 256, 256 };                       /* 0.25 m cells */
    float pts[9] = { 5.0f, 5.0f, 10.12f,     /* tile (0,0,0) on the quad */
                     40.0f, 40.0f, 10.12f,   /* tile (1,1,0) on the quad */
                     32.0f, 32.0f, 40.0f };  /* far off the quad */
    float area[3], alb[9], emi[9];
    CHECK(lm_gpu_voxelize_sample(&m, 1, &box, dims, pts, 3, area, alb, emi),
          "tiled sample failed");
    for (int i = 0; i < 2; ++i) {
        CHECK(area[i] > 0.0f, "on-quad point has no area");
        CHECK(fabsf(alb[i * 3 + 0] - 0.3f) < 0.1f &&
              fabsf(alb[i * 3 + 1] - 0.9f) < 0.1f &&
              fabsf(alb[i * 3 + 2] - 0.2f) < 0.1f,
              "tiled point albedo != tint");
    }
    CHECK(area[2] <= 0.0f, "off-quad point has surface area");
}

static uint32_t count_solid_leaves(const npc_svo_grid_t *svo)
{
    uint32_t nsolid = 0;
    for (uint32_t i = 0; i < svo->node_count; ++i)
        if (svo->nodes[i].occupancy == 0 &&
            (svo->nodes[i].flags & NPC_SVO_FLAG_SOLID))
            ++nsolid;
    return nsolid;
}

static void t_gpu_svo_build(void)
{
    build_box(0.6f, 2.4f);
    lm_mesh_t m = mesh_of(s_box_pos, s_box_nrm, s_box_uv, 24, s_box_idx, 36,
                          0.8f, 0.2f, 0.1f, 1.0f);
    lm_mesh_scene_t scn;
    memset(&scn, 0, sizeof scn);
    scn.meshes = &m;
    scn.n_meshes = 1;
    phys_aabb_t box = { { -0.5f, -0.5f, -0.5f }, { 3.5f, 3.5f, 3.5f } };

    npc_svo_grid_t gsvo;
    CHECK(lm_gpu_chunk_svo_build(&scn, box, 0.5f, &gsvo),
          "gpu svo build failed");
    uint32_t node = 0;
    uint8_t fl = npc_svo_query_point(&gsvo,
                                     (phys_vec3_t){ 0.6f, 1.5f, 1.5f }, &node);
    CHECK((fl & NPC_SVO_FLAG_SOLID) != 0, "cube face leaf not solid");
    CHECK(node < gsvo.node_count &&
          fabsf(gsvo.nodes[node].diffuse[0] - 0.8f) < 0.08f &&
          fabsf(gsvo.nodes[node].diffuse[1] - 0.2f) < 0.08f &&
          fabsf(gsvo.nodes[node].diffuse[2] - 0.1f) < 0.08f,
          "face leaf diffuse != tint");
    fl = npc_svo_query_point(&gsvo, (phys_vec3_t){ 1.5f, 1.5f, 1.5f }, &node);
    CHECK((fl & NPC_SVO_FLAG_SOLID) == 0, "cube interior solid");

    /* leaf-set parity vs the CPU stamp+subsample build (raster vs tight-stamp
     * shells differ slightly; counts must be the same order). */
    npc_svo_grid_t csvo;
    CHECK(lm_chunk_svo_build(&scn, box, 0.5f, true, &csvo),
          "cpu reference build failed");
    uint32_t gn = count_solid_leaves(&gsvo), cn = count_solid_leaves(&csvo);
    CHECK(gn > 0 && cn > 0 && gn * 2u >= cn && cn * 2u >= gn,
          "gpu/cpu solid leaf counts diverge");
    npc_svo_grid_destroy(&gsvo);
    npc_svo_grid_destroy(&csvo);
}

int main(void)
{
    /* Pre-init failure mode first: run() without init must fail cleanly. */
    {
        lm_gpu_vox_grid_t g;
        phys_aabb_t box = { { 0, 0, 0 }, { 1, 1, 1 } };
        int dims[3] = { 2, 2, 2 };
        ++g_checks;
        if (lm_gpu_voxelize_run(NULL, 0, &box, dims, &g)) {
            ++g_fails; printf("FAIL main: run before init succeeded\n");
        }
    }
    if (!egl_headless_init(4, 3)) {
        printf("SKIP lm_gpu_voxelize_tests: no EGL 4.3 context\n");
        return 0;
    }
    gl_loader_t loader = { egl_headless_getproc, NULL };
    if (!lm_gpu_voxelize_init(&loader)) {
        printf("SKIP lm_gpu_voxelize_tests: voxelizer init failed\n");
        egl_headless_shutdown();
        return 0;
    }
    t_fail_args();
    t_empty_scene();
    t_cube();
    t_edge_on_plane();
    t_textured_albedo();
    t_emissive_sum();
    t_transmission();
    t_sample_points();
    t_sample_tiled();
    t_gpu_svo_build();
    lm_gpu_voxelize_shutdown();
    egl_headless_shutdown();
    printf("lm_gpu_voxelize_tests: %d checks, %d failures\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}

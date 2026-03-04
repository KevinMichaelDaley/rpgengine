/**
 * @file p004_renderer_skeletal_mesh_tests.c
 * @brief Tests for skeletal_mesh_t: creation, destruction, bone VBOs,
 *        FVMA loading with bone data, and inverse bind matrices.
 *
 * Requires an OpenGL 3.3 context (SDL2 + GLAD).
 */

#include <glad/glad.h>
#include <SDL2/SDL.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/mesh/skeletal_mesh.h"
#include "ferrum/editor/mesh/mesh_slot.h"
#include "ferrum/editor/mesh/mesh_vao_format.h"

/* ── Test macros ──────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", \
                __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

#define ASSERT_INT_EQ(exp, act) do { \
    if ((exp) != (act)) { \
        fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n", \
                __FILE__, __LINE__, (int)(exp), (int)(act)); \
        return 1; \
    } \
} while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps) do { \
    if (fabsf((float)(exp) - (float)(act)) > (eps)) { \
        fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: " \
                "expected %.6f got %.6f (eps=%.6f)\n", \
                __FILE__, __LINE__, (double)(exp), (double)(act), (double)(eps)); \
        return 1; \
    } \
} while (0)

/* ── GL context ───────────────────────────────────────────────────── */

static gl_loader_t g_loader;

static void *sdl_get_proc_address(const char *name, void *user_data) {
    (void)user_data;
    return SDL_GL_GetProcAddress(name);
}

struct gl_test_context {
    SDL_Window    *window;
    SDL_GLContext  context;
};

static int gl_test_context_init(struct gl_test_context *ctx) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    ctx->window = SDL_CreateWindow("p004_skeletal_mesh_tests",
                                   SDL_WINDOWPOS_UNDEFINED,
                                   SDL_WINDOWPOS_UNDEFINED,
                                   64, 64,
                                   SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!ctx->window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }
    ctx->context = SDL_GL_CreateContext(ctx->window);
    if (!ctx->context) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }
    SDL_GL_MakeCurrent(ctx->window, ctx->context);
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "gladLoadGLLoader failed\n");
        SDL_GL_DeleteContext(ctx->context);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }
    (void)glGetError();
    g_loader.get_proc_address = sdl_get_proc_address;
    g_loader.user_data = NULL;
    return 0;
}

static void gl_test_context_shutdown(struct gl_test_context *ctx) {
    if (ctx->context) SDL_GL_DeleteContext(ctx->context);
    if (ctx->window)  SDL_DestroyWindow(ctx->window);
    SDL_Quit();
}

/* ── Helper: build a simple skinned triangle ─────────────────────── */

/** Builds create info for a 3-vertex triangle with 2 bones. */
static void build_skinned_triangle_(skeletal_mesh_create_info_t *info,
                                    float *positions,
                                    float *normals,
                                    uint32_t *indices,
                                    float *bone_weights,
                                    uint32_t *bone_indices,
                                    float *inv_binds)
{
    /* 3 vertices. */
    positions[0] = 0.0f; positions[1] = 0.0f; positions[2] = 0.0f;
    positions[3] = 1.0f; positions[4] = 0.0f; positions[5] = 0.0f;
    positions[6] = 0.0f; positions[7] = 1.0f; positions[8] = 0.0f;

    normals[0] = 0.0f; normals[1] = 0.0f; normals[2] = 1.0f;
    normals[3] = 0.0f; normals[4] = 0.0f; normals[5] = 1.0f;
    normals[6] = 0.0f; normals[7] = 0.0f; normals[8] = 1.0f;

    indices[0] = 0; indices[1] = 1; indices[2] = 2;

    /* Vertex 0: bone 0 weight 1.0, Vertex 1: bone 1 weight 1.0,
     * Vertex 2: 50/50 blend of bones 0 and 1. */
    bone_weights[0]  = 1.0f; bone_weights[1]  = 0.0f;
    bone_weights[2]  = 0.0f; bone_weights[3]  = 0.0f;
    bone_weights[4]  = 0.0f; bone_weights[5]  = 1.0f;
    bone_weights[6]  = 0.0f; bone_weights[7]  = 0.0f;
    bone_weights[8]  = 0.5f; bone_weights[9]  = 0.5f;
    bone_weights[10] = 0.0f; bone_weights[11] = 0.0f;

    bone_indices[0]  = 0; bone_indices[1]  = 0;
    bone_indices[2]  = 0; bone_indices[3]  = 0;
    bone_indices[4]  = 1; bone_indices[5]  = 0;
    bone_indices[6]  = 0; bone_indices[7]  = 0;
    bone_indices[8]  = 0; bone_indices[9]  = 1;
    bone_indices[10] = 0; bone_indices[11] = 0;

    /* Identity inverse-bind matrices (2 bones × 16 floats). */
    memset(inv_binds, 0, 2 * 16 * sizeof(float));
    inv_binds[0]  = 1.0f; inv_binds[5]  = 1.0f;
    inv_binds[10] = 1.0f; inv_binds[15] = 1.0f;
    inv_binds[16] = 1.0f; inv_binds[21] = 1.0f;
    inv_binds[26] = 1.0f; inv_binds[31] = 1.0f;

    memset(info, 0, sizeof(*info));
    info->base.positions    = positions;
    info->base.normals      = normals;
    info->base.indices      = indices;
    info->base.vertex_count = 3;
    info->base.index_count  = 3;
    info->bone_weights      = bone_weights;
    info->bone_indices      = bone_indices;
    info->bone_count        = 2;
    info->inv_bind_matrices = inv_binds;
}

/* ═══════════════════════════════════════════════════════════════════
 * Happy-path tests
 * ═══════════════════════════════════════════════════════════════════ */

/* ── Test: create skeletal mesh from raw data ────────────────────── */
static int test_create_from_raw_data(void) {
    float positions[9], normals[9], bone_weights[12];
    uint32_t indices[3], bone_indices[12];
    float inv_binds[32];
    skeletal_mesh_create_info_t info;
    build_skinned_triangle_(&info, positions, normals, indices,
                            bone_weights, bone_indices, inv_binds);

    skeletal_mesh_t mesh;
    int rc = skeletal_mesh_create(&g_loader, &info, &mesh);
    ASSERT_INT_EQ(SKELETAL_MESH_OK, rc);

    /* Base mesh fields. */
    ASSERT_INT_EQ(3, (int)mesh.base.vertex_count);
    ASSERT_INT_EQ(3, (int)mesh.base.index_count);
    ASSERT_INT_EQ(1, (int)mesh.base.submesh_count);
    ASSERT_TRUE(mesh.base.vao.handle != 0);
    ASSERT_TRUE(mesh.base.vbo_position.handle != 0);
    ASSERT_TRUE(mesh.base.ibo.handle != 0);

    /* Bone-specific fields. */
    ASSERT_TRUE(mesh.vbo_bone_weights.handle != 0);
    ASSERT_TRUE(mesh.vbo_bone_indices.handle != 0);
    ASSERT_INT_EQ(2, (int)mesh.bone_count);
    ASSERT_TRUE(mesh.inv_bind_matrices != NULL);

    /* Inverse bind matrix spot-check (identity). */
    ASSERT_FLOAT_NEAR(1.0f, mesh.inv_bind_matrices[0], 0.001f);
    ASSERT_FLOAT_NEAR(1.0f, mesh.inv_bind_matrices[16], 0.001f);

    skeletal_mesh_destroy(&mesh);
    ASSERT_INT_EQ(0, (int)mesh.base.vao.handle);
    return 0;
}

/* ── Test: bone VBOs are separate from base mesh VBOs ────────────── */
static int test_bone_vbos_are_distinct(void) {
    float positions[9], normals[9], bone_weights[12];
    uint32_t indices[3], bone_indices[12];
    float inv_binds[32];
    skeletal_mesh_create_info_t info;
    build_skinned_triangle_(&info, positions, normals, indices,
                            bone_weights, bone_indices, inv_binds);

    skeletal_mesh_t mesh;
    int rc = skeletal_mesh_create(&g_loader, &info, &mesh);
    ASSERT_INT_EQ(SKELETAL_MESH_OK, rc);

    /* Bone VBO handles must differ from all base handles. */
    ASSERT_TRUE(mesh.vbo_bone_weights.handle != mesh.base.vbo_position.handle);
    ASSERT_TRUE(mesh.vbo_bone_weights.handle != mesh.base.vbo_normal.handle);
    ASSERT_TRUE(mesh.vbo_bone_indices.handle != mesh.base.vbo_position.handle);
    ASSERT_TRUE(mesh.vbo_bone_weights.handle != mesh.vbo_bone_indices.handle);

    skeletal_mesh_destroy(&mesh);
    return 0;
}

/* ── Test: draw skeletal mesh (smoke) ─────────────────────────────── */
static int test_draw_smoke(void) {
    float positions[9], normals[9], bone_weights[12];
    uint32_t indices[3], bone_indices[12];
    float inv_binds[32];
    skeletal_mesh_create_info_t info;
    build_skinned_triangle_(&info, positions, normals, indices,
                            bone_weights, bone_indices, inv_binds);

    skeletal_mesh_t mesh;
    int rc = skeletal_mesh_create(&g_loader, &info, &mesh);
    ASSERT_INT_EQ(SKELETAL_MESH_OK, rc);

    /* Bind and draw — must not crash. */
    skeletal_mesh_bind(&mesh);
    glGetError();
    skeletal_mesh_draw_submesh(&mesh, 0);
    (void)glGetError();
    skeletal_mesh_unbind();

    skeletal_mesh_destroy(&mesh);
    return 0;
}

/* ── Test: base mesh optional attrs work with skeletal ────────────── */
static int test_optional_attrs_with_bones(void) {
    float positions[9], normals[9], bone_weights[12];
    uint32_t indices[3], bone_indices[12];
    float inv_binds[32];
    skeletal_mesh_create_info_t info;
    build_skinned_triangle_(&info, positions, normals, indices,
                            bone_weights, bone_indices, inv_binds);

    /* Add tangents and UV0. */
    float tangents[12] = {
        1, 0, 0, 1,  1, 0, 0, 1,  1, 0, 0, 1
    };
    float uv0[6] = { 0, 0, 1, 0, 0, 1 };
    info.base.tangents = tangents;
    info.base.uv0      = uv0;

    skeletal_mesh_t mesh;
    int rc = skeletal_mesh_create(&g_loader, &info, &mesh);
    ASSERT_INT_EQ(SKELETAL_MESH_OK, rc);

    ASSERT_TRUE(mesh.base.vbo_tangent.handle != 0);
    ASSERT_TRUE(mesh.base.vbo_uv0.handle != 0);
    ASSERT_TRUE(mesh.vbo_bone_weights.handle != 0);

    skeletal_mesh_destroy(&mesh);
    return 0;
}

/* ── Test: inv_bind_matrices are deep-copied ─────────────────────── */
static int test_inv_bind_deep_copy(void) {
    float positions[9], normals[9], bone_weights[12];
    uint32_t indices[3], bone_indices[12];
    float inv_binds[32];
    skeletal_mesh_create_info_t info;
    build_skinned_triangle_(&info, positions, normals, indices,
                            bone_weights, bone_indices, inv_binds);

    skeletal_mesh_t mesh;
    int rc = skeletal_mesh_create(&g_loader, &info, &mesh);
    ASSERT_INT_EQ(SKELETAL_MESH_OK, rc);

    /* Modify source — mesh copy must be unaffected. */
    inv_binds[0] = 999.0f;
    ASSERT_FLOAT_NEAR(1.0f, mesh.inv_bind_matrices[0], 0.001f);

    skeletal_mesh_destroy(&mesh);
    return 0;
}

/* ── Test: max bone count (128) ──────────────────────────────────── */
static int test_max_bone_count(void) {
    float positions[9], normals[9], bone_weights[12];
    uint32_t indices[3], bone_indices[12];
    skeletal_mesh_create_info_t info;

    /* 128 identity matrices. */
    float inv_binds[128 * 16];
    memset(inv_binds, 0, sizeof(inv_binds));
    for (int i = 0; i < 128; i++) {
        inv_binds[i * 16 + 0]  = 1.0f;
        inv_binds[i * 16 + 5]  = 1.0f;
        inv_binds[i * 16 + 10] = 1.0f;
        inv_binds[i * 16 + 15] = 1.0f;
    }

    build_skinned_triangle_(&info, positions, normals, indices,
                            bone_weights, bone_indices, inv_binds);
    info.bone_count        = 128;
    info.inv_bind_matrices = inv_binds;

    skeletal_mesh_t mesh;
    int rc = skeletal_mesh_create(&g_loader, &info, &mesh);
    ASSERT_INT_EQ(SKELETAL_MESH_OK, rc);
    ASSERT_INT_EQ(128, (int)mesh.bone_count);

    skeletal_mesh_destroy(&mesh);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 * Edge-case / failure tests
 * ═══════════════════════════════════════════════════════════════════ */

/* ── Test: NULL loader returns error ──────────────────────────────── */
static int test_null_loader(void) {
    skeletal_mesh_create_info_t info;
    memset(&info, 0, sizeof(info));
    skeletal_mesh_t mesh;
    int rc = skeletal_mesh_create(NULL, &info, &mesh);
    ASSERT_INT_EQ(SKELETAL_MESH_ERR_INVALID, rc);
    return 0;
}

/* ── Test: NULL output returns error ──────────────────────────────── */
static int test_null_output(void) {
    skeletal_mesh_create_info_t info;
    memset(&info, 0, sizeof(info));
    int rc = skeletal_mesh_create(&g_loader, &info, NULL);
    ASSERT_INT_EQ(SKELETAL_MESH_ERR_INVALID, rc);
    return 0;
}

/* ── Test: missing bone weights returns error ────────────────────── */
static int test_missing_bone_weights(void) {
    float positions[9], normals[9], bone_weights[12];
    uint32_t indices[3], bone_indices[12];
    float inv_binds[32];
    skeletal_mesh_create_info_t info;
    build_skinned_triangle_(&info, positions, normals, indices,
                            bone_weights, bone_indices, inv_binds);
    info.bone_weights = NULL; /* override after build */

    skeletal_mesh_t mesh;
    int rc = skeletal_mesh_create(&g_loader, &info, &mesh);
    ASSERT_INT_EQ(SKELETAL_MESH_ERR_INVALID, rc);
    return 0;
}

/* ── Test: missing bone indices returns error ────────────────────── */
static int test_missing_bone_indices(void) {
    float positions[9], normals[9], bone_weights[12];
    uint32_t indices[3], bone_indices[12];
    float inv_binds[32];
    skeletal_mesh_create_info_t info;
    build_skinned_triangle_(&info, positions, normals, indices,
                            bone_weights, bone_indices, inv_binds);
    info.bone_indices = NULL; /* override after build */

    skeletal_mesh_t mesh;
    int rc = skeletal_mesh_create(&g_loader, &info, &mesh);
    ASSERT_INT_EQ(SKELETAL_MESH_ERR_INVALID, rc);
    return 0;
}

/* ── Test: zero bone count returns error ──────────────────────────── */
static int test_zero_bone_count(void) {
    float positions[9], normals[9], bone_weights[12];
    uint32_t indices[3], bone_indices[12];
    float inv_binds[32];
    skeletal_mesh_create_info_t info;
    build_skinned_triangle_(&info, positions, normals, indices,
                            bone_weights, bone_indices, inv_binds);
    info.bone_count = 0;

    skeletal_mesh_t mesh;
    int rc = skeletal_mesh_create(&g_loader, &info, &mesh);
    ASSERT_INT_EQ(SKELETAL_MESH_ERR_INVALID, rc);
    return 0;
}

/* ── Test: missing inv_bind_matrices returns error ────────────────── */
static int test_missing_inv_bind(void) {
    float positions[9], normals[9], bone_weights[12];
    uint32_t indices[3], bone_indices[12];
    float inv_binds[32];
    skeletal_mesh_create_info_t info;
    build_skinned_triangle_(&info, positions, normals, indices,
                            bone_weights, bone_indices, inv_binds);
    info.inv_bind_matrices = NULL;

    skeletal_mesh_t mesh;
    int rc = skeletal_mesh_create(&g_loader, &info, &mesh);
    ASSERT_INT_EQ(SKELETAL_MESH_ERR_INVALID, rc);
    return 0;
}

/* ── Test: bone count over 128 returns error ─────────────────────── */
static int test_bone_count_over_max(void) {
    float positions[9], normals[9], bone_weights[12];
    uint32_t indices[3], bone_indices[12];
    float inv_binds[32];
    skeletal_mesh_create_info_t info;
    build_skinned_triangle_(&info, positions, normals, indices,
                            bone_weights, bone_indices, inv_binds);
    info.bone_count = 129;

    skeletal_mesh_t mesh;
    int rc = skeletal_mesh_create(&g_loader, &info, &mesh);
    ASSERT_INT_EQ(SKELETAL_MESH_ERR_INVALID, rc);
    return 0;
}

/* ── Test: double destroy is safe ─────────────────────────────────── */
static int test_double_destroy(void) {
    float positions[9], normals[9], bone_weights[12];
    uint32_t indices[3], bone_indices[12];
    float inv_binds[32];
    skeletal_mesh_create_info_t info;
    build_skinned_triangle_(&info, positions, normals, indices,
                            bone_weights, bone_indices, inv_binds);

    skeletal_mesh_t mesh;
    int rc = skeletal_mesh_create(&g_loader, &info, &mesh);
    ASSERT_INT_EQ(SKELETAL_MESH_OK, rc);

    skeletal_mesh_destroy(&mesh);
    skeletal_mesh_destroy(&mesh); /* must not crash */
    return 0;
}

/* ── Test: destroy NULL is safe ───────────────────────────────────── */
static int test_destroy_null(void) {
    skeletal_mesh_destroy(NULL); /* must not crash */
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 * Test runner
 * ═══════════════════════════════════════════════════════════════════ */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    /* Happy path */
    {"create_from_raw_data",       test_create_from_raw_data},
    {"bone_vbos_are_distinct",     test_bone_vbos_are_distinct},
    {"draw_smoke",                 test_draw_smoke},
    {"optional_attrs_with_bones",  test_optional_attrs_with_bones},
    {"inv_bind_deep_copy",         test_inv_bind_deep_copy},
    {"max_bone_count",             test_max_bone_count},
    /* Edge / failure */
    {"null_loader",                test_null_loader},
    {"null_output",                test_null_output},
    {"missing_bone_weights",       test_missing_bone_weights},
    {"missing_bone_indices",       test_missing_bone_indices},
    {"zero_bone_count",            test_zero_bone_count},
    {"missing_inv_bind",           test_missing_inv_bind},
    {"bone_count_over_max",        test_bone_count_over_max},
    {"double_destroy",             test_double_destroy},
    {"destroy_null",               test_destroy_null},
};

int main(void) {
    struct gl_test_context ctx = {0};
    if (gl_test_context_init(&ctx) != 0) return 1;

    size_t total = sizeof(TESTS) / sizeof(TESTS[0]);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        printf("RUN  %s\n", TESTS[i].name);
        int rc = TESTS[i].fn();
        if (rc == 0) {
            printf("  OK %s\n", TESTS[i].name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s (rc=%d)\n", TESTS[i].name, rc);
        }
    }

    gl_test_context_shutdown(&ctx);
    printf("\n%zu / %zu tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}

/**
 * @file p004_renderer_mesh_registry_tests.c
 * @brief Tests for mesh_registry_t: handle-based mesh storage with
 *        configurable capacity and generation-guarded lookup.
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
#include "ferrum/renderer/mesh/mesh_handle.h"
#include "ferrum/renderer/mesh/mesh_registry.h"
#include "ferrum/renderer/mesh/static_mesh.h"
#include "ferrum/renderer/mesh/skeletal_mesh.h"

/* ── Test macros ──────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", \
                __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

#define ASSERT_FALSE(cond) do { \
    if ((cond)) { \
        fprintf(stderr, "ASSERT_FALSE failed: %s:%d: %s\n", \
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

#define ASSERT_UINT_EQ(exp, act) do { \
    if ((exp) != (act)) { \
        fprintf(stderr, "ASSERT_UINT_EQ failed: %s:%d: expected %u got %u\n", \
                __FILE__, __LINE__, (unsigned)(exp), (unsigned)(act)); \
        return 1; \
    } \
} while (0)

#define ASSERT_PTR_NOT_NULL(p) do { \
    if ((p) == NULL) { \
        fprintf(stderr, "ASSERT_PTR_NOT_NULL failed: %s:%d: %s\n", \
                __FILE__, __LINE__, #p); \
        return 1; \
    } \
} while (0)

#define ASSERT_PTR_NULL(p) do { \
    if ((p) != NULL) { \
        fprintf(stderr, "ASSERT_PTR_NULL failed: %s:%d: %s\n", \
                __FILE__, __LINE__, #p); \
        return 1; \
    } \
} while (0)

/* ── GL context ───────────────────────────────────────────────────── */

struct gl_test_context {
    SDL_Window    *window;
    SDL_GLContext  context;
};

static gl_loader_t g_loader;

static void *sdl_get_proc_address(const char *name, void *user_data) {
    (void)user_data;
    return SDL_GL_GetProcAddress(name);
}

static int gl_test_context_init(struct gl_test_context *ctx) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,
                        SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);

    ctx->window = SDL_CreateWindow("p004_mesh_registry_tests",
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
    if (SDL_GL_MakeCurrent(ctx->window, ctx->context) != 0) {
        fprintf(stderr, "SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
        SDL_GL_DeleteContext(ctx->context);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }
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

static void gl_test_context_cleanup(struct gl_test_context *ctx) {
    SDL_GL_DeleteContext(ctx->context);
    SDL_DestroyWindow(ctx->window);
    SDL_Quit();
}

/* ── Helpers ──────────────────────────────────────────────────────── */

/** Build a minimal static mesh create info (3-vert triangle). */
static void build_triangle_info_(static_mesh_create_info_t *info,
                                 float *positions, float *normals,
                                 uint32_t *indices)
{
    positions[0] = 0.0f; positions[1] = 0.0f; positions[2] = 0.0f;
    positions[3] = 1.0f; positions[4] = 0.0f; positions[5] = 0.0f;
    positions[6] = 0.0f; positions[7] = 1.0f; positions[8] = 0.0f;

    normals[0] = 0.0f; normals[1] = 0.0f; normals[2] = 1.0f;
    normals[3] = 0.0f; normals[4] = 0.0f; normals[5] = 1.0f;
    normals[6] = 0.0f; normals[7] = 0.0f; normals[8] = 1.0f;

    indices[0] = 0; indices[1] = 1; indices[2] = 2;

    memset(info, 0, sizeof(*info));
    info->positions    = positions;
    info->normals      = normals;
    info->indices      = indices;
    info->vertex_count = 3;
    info->index_count  = 3;
}

/** Build a minimal skeletal mesh create info (3-vert, 2-bone triangle). */
static void build_skinned_info_(skeletal_mesh_create_info_t *info,
                                float *positions, float *normals,
                                uint32_t *indices,
                                float *bone_weights,
                                uint32_t *bone_indices,
                                float *inv_binds)
{
    build_triangle_info_(&info->base, positions, normals, indices);

    /* Vertex 0: bone 0 full weight.  Vertex 1: bone 1 full weight.
     * Vertex 2: 50/50 blend. */
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

    /* 2 identity matrices (16 floats each). */
    memset(inv_binds, 0, 32 * sizeof(float));
    inv_binds[0]  = 1.0f; inv_binds[5]  = 1.0f;
    inv_binds[10] = 1.0f; inv_binds[15] = 1.0f;
    inv_binds[16] = 1.0f; inv_binds[21] = 1.0f;
    inv_binds[26] = 1.0f; inv_binds[31] = 1.0f;

    info->bone_weights      = bone_weights;
    info->bone_indices       = bone_indices;
    info->bone_count         = 2;
    info->inv_bind_matrices  = inv_binds;
}

/* ═══════════════════════════════════════════════════════════════════
 *  HAPPY PATH
 * ═══════════════════════════════════════════════════════════════════ */

/* ── Test: init sets count to zero and capacity ─────────────────── */
static int test_init_and_destroy(void) {
    mesh_registry_t reg;
    int rc = mesh_registry_init(&reg, 64, &g_loader);
    ASSERT_INT_EQ(MESH_REGISTRY_OK, rc);
    ASSERT_UINT_EQ(0, mesh_registry_count(&reg));
    ASSERT_UINT_EQ(64, mesh_registry_capacity(&reg));
    mesh_registry_destroy(&reg);
    return 0;
}

/* ── Test: insert static mesh returns valid handle ──────────────── */
static int test_insert_static(void) {
    mesh_registry_t reg;
    mesh_registry_init(&reg, 16, &g_loader);

    float pos[9], nrm[9];
    uint32_t idx[3];
    static_mesh_create_info_t info;
    build_triangle_info_(&info, pos, nrm, idx);

    mesh_handle_t handle;
    int rc = mesh_registry_insert_static(&reg, &info, &handle);
    ASSERT_INT_EQ(MESH_REGISTRY_OK, rc);
    ASSERT_TRUE(mesh_registry_is_valid(&reg, handle));
    ASSERT_UINT_EQ(1, mesh_registry_count(&reg));

    mesh_registry_destroy(&reg);
    return 0;
}

/* ── Test: insert skeletal mesh returns valid handle ─────────────── */
static int test_insert_skeletal(void) {
    mesh_registry_t reg;
    mesh_registry_init(&reg, 16, &g_loader);

    float pos[9], nrm[9], bw[12], inv[32];
    uint32_t idx[3], bi[12];
    skeletal_mesh_create_info_t info;
    build_skinned_info_(&info, pos, nrm, idx, bw, bi, inv);

    mesh_handle_t handle;
    int rc = mesh_registry_insert_skeletal(&reg, &info, &handle);
    ASSERT_INT_EQ(MESH_REGISTRY_OK, rc);
    ASSERT_TRUE(mesh_registry_is_valid(&reg, handle));
    ASSERT_UINT_EQ(1, mesh_registry_count(&reg));

    mesh_registry_destroy(&reg);
    return 0;
}

/* ── Test: get_static returns non-NULL pointer to inserted mesh ─── */
static int test_get_static(void) {
    mesh_registry_t reg;
    mesh_registry_init(&reg, 16, &g_loader);

    float pos[9], nrm[9];
    uint32_t idx[3];
    static_mesh_create_info_t info;
    build_triangle_info_(&info, pos, nrm, idx);

    mesh_handle_t handle;
    mesh_registry_insert_static(&reg, &info, &handle);

    const static_mesh_t *mesh = mesh_registry_get_static(&reg, handle);
    ASSERT_PTR_NOT_NULL(mesh);
    ASSERT_UINT_EQ(3, mesh->vertex_count);
    ASSERT_UINT_EQ(3, mesh->index_count);

    mesh_registry_destroy(&reg);
    return 0;
}

/* ── Test: get_skeletal returns non-NULL pointer ─────────────────── */
static int test_get_skeletal(void) {
    mesh_registry_t reg;
    mesh_registry_init(&reg, 16, &g_loader);

    float pos[9], nrm[9], bw[12], inv[32];
    uint32_t idx[3], bi[12];
    skeletal_mesh_create_info_t info;
    build_skinned_info_(&info, pos, nrm, idx, bw, bi, inv);

    mesh_handle_t handle;
    mesh_registry_insert_skeletal(&reg, &info, &handle);

    const skeletal_mesh_t *mesh = mesh_registry_get_skeletal(&reg, handle);
    ASSERT_PTR_NOT_NULL(mesh);
    ASSERT_UINT_EQ(2, mesh->bone_count);

    mesh_registry_destroy(&reg);
    return 0;
}

/* ── Test: type query returns correct type ──────────────────────── */
static int test_type_query(void) {
    mesh_registry_t reg;
    mesh_registry_init(&reg, 16, &g_loader);

    float pos[9], nrm[9];
    uint32_t idx[3];
    static_mesh_create_info_t sinfo;
    build_triangle_info_(&sinfo, pos, nrm, idx);

    mesh_handle_t h_static;
    mesh_registry_insert_static(&reg, &sinfo, &h_static);
    ASSERT_INT_EQ(MESH_TYPE_STATIC, mesh_registry_type(&reg, h_static));

    float bw[12], inv[32];
    uint32_t bi[12];
    skeletal_mesh_create_info_t kinfo;
    build_skinned_info_(&kinfo, pos, nrm, idx, bw, bi, inv);

    mesh_handle_t h_skel;
    mesh_registry_insert_skeletal(&reg, &kinfo, &h_skel);
    ASSERT_INT_EQ(MESH_TYPE_SKELETAL, mesh_registry_type(&reg, h_skel));

    mesh_registry_destroy(&reg);
    return 0;
}

/* ── Test: remove decrements count and invalidates handle ────────── */
static int test_remove(void) {
    mesh_registry_t reg;
    mesh_registry_init(&reg, 16, &g_loader);

    float pos[9], nrm[9];
    uint32_t idx[3];
    static_mesh_create_info_t info;
    build_triangle_info_(&info, pos, nrm, idx);

    mesh_handle_t handle;
    mesh_registry_insert_static(&reg, &info, &handle);
    ASSERT_UINT_EQ(1, mesh_registry_count(&reg));

    mesh_registry_remove(&reg, handle);
    ASSERT_UINT_EQ(0, mesh_registry_count(&reg));
    ASSERT_FALSE(mesh_registry_is_valid(&reg, handle));

    mesh_registry_destroy(&reg);
    return 0;
}

/* ── Test: generation prevents use-after-free ───────────────────── */
static int test_generation_invalidates(void) {
    mesh_registry_t reg;
    mesh_registry_init(&reg, 16, &g_loader);

    float pos[9], nrm[9];
    uint32_t idx[3];
    static_mesh_create_info_t info;
    build_triangle_info_(&info, pos, nrm, idx);

    /* Insert, get handle, remove. */
    mesh_handle_t old_handle;
    mesh_registry_insert_static(&reg, &info, &old_handle);
    mesh_registry_remove(&reg, old_handle);

    /* Insert again — reuses same slot but bumped generation. */
    mesh_handle_t new_handle;
    mesh_registry_insert_static(&reg, &info, &new_handle);

    /* Old handle must be invalid, new handle must be valid. */
    ASSERT_FALSE(mesh_registry_is_valid(&reg, old_handle));
    ASSERT_TRUE(mesh_registry_is_valid(&reg, new_handle));

    /* Same slot, different generation. */
    ASSERT_UINT_EQ(old_handle.index, new_handle.index);
    ASSERT_TRUE(old_handle.generation != new_handle.generation);

    /* Lookup with stale handle must return NULL. */
    ASSERT_PTR_NULL(mesh_registry_get_static(&reg, old_handle));
    ASSERT_PTR_NOT_NULL(mesh_registry_get_static(&reg, new_handle));

    mesh_registry_destroy(&reg);
    return 0;
}

/* ── Test: multiple inserts all valid ────────────────────────────── */
static int test_multiple_inserts(void) {
    mesh_registry_t reg;
    mesh_registry_init(&reg, 64, &g_loader);

    float pos[9], nrm[9];
    uint32_t idx[3];
    static_mesh_create_info_t info;
    build_triangle_info_(&info, pos, nrm, idx);

    mesh_handle_t handles[8];
    for (int i = 0; i < 8; ++i) {
        int rc = mesh_registry_insert_static(&reg, &info, &handles[i]);
        ASSERT_INT_EQ(MESH_REGISTRY_OK, rc);
    }
    ASSERT_UINT_EQ(8, mesh_registry_count(&reg));

    for (int i = 0; i < 8; ++i) {
        ASSERT_TRUE(mesh_registry_is_valid(&reg, handles[i]));
    }

    mesh_registry_destroy(&reg);
    return 0;
}

/* ── Test: get wrong type returns NULL ───────────────────────────── */
static int test_get_wrong_type(void) {
    mesh_registry_t reg;
    mesh_registry_init(&reg, 16, &g_loader);

    float pos[9], nrm[9];
    uint32_t idx[3];
    static_mesh_create_info_t info;
    build_triangle_info_(&info, pos, nrm, idx);

    mesh_handle_t handle;
    mesh_registry_insert_static(&reg, &info, &handle);

    /* Asking for skeletal when it's static should return NULL. */
    ASSERT_PTR_NULL(mesh_registry_get_skeletal(&reg, handle));

    mesh_registry_destroy(&reg);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  EDGE CASES
 * ═══════════════════════════════════════════════════════════════════ */

/* ── Test: full registry rejects further inserts ────────────────── */
static int test_full_registry(void) {
    mesh_registry_t reg;
    mesh_registry_init(&reg, 4, &g_loader);

    float pos[9], nrm[9];
    uint32_t idx[3];
    static_mesh_create_info_t info;
    build_triangle_info_(&info, pos, nrm, idx);

    mesh_handle_t h;
    for (int i = 0; i < 4; ++i) {
        ASSERT_INT_EQ(MESH_REGISTRY_OK,
                       mesh_registry_insert_static(&reg, &info, &h));
    }
    ASSERT_UINT_EQ(4, mesh_registry_count(&reg));

    /* 5th insert should fail. */
    int rc = mesh_registry_insert_static(&reg, &info, &h);
    ASSERT_INT_EQ(MESH_REGISTRY_ERR_FULL, rc);

    mesh_registry_destroy(&reg);
    return 0;
}

/* ── Test: remove then re-insert recycles slot ──────────────────── */
static int test_slot_reuse(void) {
    mesh_registry_t reg;
    mesh_registry_init(&reg, 4, &g_loader);

    float pos[9], nrm[9];
    uint32_t idx[3];
    static_mesh_create_info_t info;
    build_triangle_info_(&info, pos, nrm, idx);

    /* Fill all 4 slots. */
    mesh_handle_t handles[4];
    for (int i = 0; i < 4; ++i) {
        mesh_registry_insert_static(&reg, &info, &handles[i]);
    }
    /* Remove slot 1. */
    mesh_registry_remove(&reg, handles[1]);
    ASSERT_UINT_EQ(3, mesh_registry_count(&reg));

    /* Insert again — must succeed, reusing slot 1. */
    mesh_handle_t new_handle;
    int rc = mesh_registry_insert_static(&reg, &info, &new_handle);
    ASSERT_INT_EQ(MESH_REGISTRY_OK, rc);
    ASSERT_UINT_EQ(4, mesh_registry_count(&reg));
    ASSERT_UINT_EQ(handles[1].index, new_handle.index);

    mesh_registry_destroy(&reg);
    return 0;
}

/* ── Test: large capacity init (65536 slots) ────────────────────── */
static int test_large_capacity(void) {
    mesh_registry_t reg;
    int rc = mesh_registry_init(&reg, 65536, &g_loader);
    ASSERT_INT_EQ(MESH_REGISTRY_OK, rc);
    ASSERT_UINT_EQ(65536, mesh_registry_capacity(&reg));
    ASSERT_UINT_EQ(0, mesh_registry_count(&reg));
    mesh_registry_destroy(&reg);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  FAILURE MODES
 * ═══════════════════════════════════════════════════════════════════ */

/* ── Test: init with NULL registry ──────────────────────────────── */
static int test_init_null_registry(void) {
    int rc = mesh_registry_init(NULL, 64, &g_loader);
    ASSERT_INT_EQ(MESH_REGISTRY_ERR_INVALID, rc);
    return 0;
}

/* ── Test: init with NULL loader ────────────────────────────────── */
static int test_init_null_loader(void) {
    mesh_registry_t reg;
    int rc = mesh_registry_init(&reg, 64, NULL);
    ASSERT_INT_EQ(MESH_REGISTRY_ERR_INVALID, rc);
    return 0;
}

/* ── Test: init with zero capacity ──────────────────────────────── */
static int test_init_zero_capacity(void) {
    mesh_registry_t reg;
    int rc = mesh_registry_init(&reg, 0, &g_loader);
    ASSERT_INT_EQ(MESH_REGISTRY_ERR_INVALID, rc);
    return 0;
}

/* ── Test: is_valid with bogus handle ───────────────────────────── */
static int test_invalid_handle(void) {
    mesh_registry_t reg;
    mesh_registry_init(&reg, 16, &g_loader);

    /* Out-of-range index. */
    mesh_handle_t bad = { .index = 9999, .generation = 0 };
    ASSERT_FALSE(mesh_registry_is_valid(&reg, bad));
    ASSERT_PTR_NULL(mesh_registry_get_static(&reg, bad));

    mesh_registry_destroy(&reg);
    return 0;
}

/* ── Test: double remove is safe (no-op) ────────────────────────── */
static int test_double_remove(void) {
    mesh_registry_t reg;
    mesh_registry_init(&reg, 16, &g_loader);

    float pos[9], nrm[9];
    uint32_t idx[3];
    static_mesh_create_info_t info;
    build_triangle_info_(&info, pos, nrm, idx);

    mesh_handle_t handle;
    mesh_registry_insert_static(&reg, &info, &handle);
    mesh_registry_remove(&reg, handle);

    /* Second remove with stale handle — should be no-op. */
    mesh_registry_remove(&reg, handle);
    ASSERT_UINT_EQ(0, mesh_registry_count(&reg));

    mesh_registry_destroy(&reg);
    return 0;
}

/* ── Test: destroy NULL is safe ─────────────────────────────────── */
static int test_destroy_null(void) {
    mesh_registry_destroy(NULL);
    return 0;
}

/* ── Test: insert with NULL info ────────────────────────────────── */
static int test_insert_null_info(void) {
    mesh_registry_t reg;
    mesh_registry_init(&reg, 16, &g_loader);

    mesh_handle_t handle;
    int rc = mesh_registry_insert_static(&reg, NULL, &handle);
    ASSERT_INT_EQ(MESH_REGISTRY_ERR_INVALID, rc);

    mesh_registry_destroy(&reg);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Test runner
 * ═══════════════════════════════════════════════════════════════════ */

typedef struct { const char *name; int (*fn)(void); } test_entry_t;

static const test_entry_t TESTS[] = {
    /* Happy path */
    {"init_and_destroy",         test_init_and_destroy},
    {"insert_static",            test_insert_static},
    {"insert_skeletal",          test_insert_skeletal},
    {"get_static",               test_get_static},
    {"get_skeletal",             test_get_skeletal},
    {"type_query",               test_type_query},
    {"remove",                   test_remove},
    {"generation_invalidates",   test_generation_invalidates},
    {"multiple_inserts",         test_multiple_inserts},
    {"get_wrong_type",           test_get_wrong_type},
    /* Edge cases */
    {"full_registry",            test_full_registry},
    {"slot_reuse",               test_slot_reuse},
    {"large_capacity",           test_large_capacity},
    /* Failure modes */
    {"init_null_registry",       test_init_null_registry},
    {"init_null_loader",         test_init_null_loader},
    {"init_zero_capacity",       test_init_zero_capacity},
    {"invalid_handle",           test_invalid_handle},
    {"double_remove",            test_double_remove},
    {"destroy_null",             test_destroy_null},
    {"insert_null_info",         test_insert_null_info},
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
            ++passed;
        } else {
            printf("FAIL %s\n", TESTS[i].name);
        }
    }
    printf("\n%zu / %zu tests passed\n", passed, total);

    gl_test_context_cleanup(&ctx);
    return (passed == total) ? 0 : 1;
}

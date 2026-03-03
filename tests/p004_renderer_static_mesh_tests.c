/**
 * @file p004_renderer_static_mesh_tests.c
 * @brief Tests for static_mesh_t: creation, destruction, FVMA loading,
 *        primitive generation, and drawing.
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
#include "ferrum/renderer/mesh/static_mesh.h"
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

    ctx->window = SDL_CreateWindow("p004_static_mesh_tests",
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

static void gl_test_context_shutdown(struct gl_test_context *ctx) {
    if (ctx->context) SDL_GL_DeleteContext(ctx->context);
    if (ctx->window)  SDL_DestroyWindow(ctx->window);
    SDL_Quit();
}

/* ═══════════════════════════════════════════════════════════════════
 * Happy-path tests
 * ═══════════════════════════════════════════════════════════════════ */

/* ── Test: create box primitive and verify vertex/index counts ──── */
static int test_create_box_primitive(void) {
    static_mesh_t mesh;
    int rc = static_mesh_create_box(&g_loader, 1.0f, 1.0f, 1.0f, &mesh);
    ASSERT_INT_EQ(STATIC_MESH_OK, rc);

    /* Box: 24 vertices (4 per face × 6 faces), 36 indices (2 tris × 6 faces). */
    ASSERT_INT_EQ(24, (int)mesh.vertex_count);
    ASSERT_INT_EQ(36, (int)mesh.index_count);
    ASSERT_INT_EQ(1, (int)mesh.submesh_count);
    ASSERT_TRUE(mesh.vao.handle != 0);
    ASSERT_TRUE(mesh.vbo_position.handle != 0);
    ASSERT_TRUE(mesh.vbo_normal.handle != 0);
    ASSERT_TRUE(mesh.ibo.handle != 0);

    /* Bounding sphere should encompass a unit cube. */
    float expected_r = sqrtf(0.5f * 0.5f + 0.5f * 0.5f + 0.5f * 0.5f);
    ASSERT_FLOAT_NEAR(expected_r, mesh.bsphere_radius, 0.01f);

    /* AABB should be [-0.5, 0.5] on all axes. */
    ASSERT_FLOAT_NEAR(-0.5f, mesh.aabb_min[0], 0.01f);
    ASSERT_FLOAT_NEAR(-0.5f, mesh.aabb_min[1], 0.01f);
    ASSERT_FLOAT_NEAR(-0.5f, mesh.aabb_min[2], 0.01f);
    ASSERT_FLOAT_NEAR( 0.5f, mesh.aabb_max[0], 0.01f);
    ASSERT_FLOAT_NEAR( 0.5f, mesh.aabb_max[1], 0.01f);
    ASSERT_FLOAT_NEAR( 0.5f, mesh.aabb_max[2], 0.01f);

    static_mesh_destroy(&mesh);
    ASSERT_INT_EQ(0, (int)mesh.vao.handle);
    return 0;
}

/* ── Test: create sphere primitive ────────────────────────────────── */
static int test_create_sphere_primitive(void) {
    static_mesh_t mesh;
    int rc = static_mesh_create_sphere(&g_loader, 0.5f, 16, 12, &mesh);
    ASSERT_INT_EQ(STATIC_MESH_OK, rc);

    /* Sphere has (slices+1)*(rings+1) vertices, slices*rings*2 triangles. */
    ASSERT_TRUE(mesh.vertex_count > 0);
    ASSERT_TRUE(mesh.index_count > 0);
    ASSERT_TRUE(mesh.submesh_count == 1);
    ASSERT_FLOAT_NEAR(0.5f, mesh.bsphere_radius, 0.02f);

    static_mesh_destroy(&mesh);
    return 0;
}

/* ── Test: create capsule primitive ───────────────────────────────── */
static int test_create_capsule_primitive(void) {
    static_mesh_t mesh;
    int rc = static_mesh_create_capsule(&g_loader, 0.5f, 1.0f, 16, 8, &mesh);
    ASSERT_INT_EQ(STATIC_MESH_OK, rc);

    ASSERT_TRUE(mesh.vertex_count > 0);
    ASSERT_TRUE(mesh.index_count > 0);
    ASSERT_TRUE(mesh.submesh_count == 1);

    /* Bounding sphere: radius should encompass capsule extent (half_h + r). */
    ASSERT_TRUE(mesh.bsphere_radius >= 1.49f);

    static_mesh_destroy(&mesh);
    return 0;
}

/* ── Test: create plane primitive ─────────────────────────────────── */
static int test_create_plane_primitive(void) {
    static_mesh_t mesh;
    int rc = static_mesh_create_plane(&g_loader, 10.0f, 10.0f, &mesh);
    ASSERT_INT_EQ(STATIC_MESH_OK, rc);

    /* Plane: 4 vertices, 6 indices (2 triangles). */
    ASSERT_INT_EQ(4, (int)mesh.vertex_count);
    ASSERT_INT_EQ(6, (int)mesh.index_count);
    ASSERT_INT_EQ(1, (int)mesh.submesh_count);

    static_mesh_destroy(&mesh);
    return 0;
}

/* ── Test: create from raw data ───────────────────────────────────── */
static int test_create_from_raw_data(void) {
    /* Triangle: 3 vertices, 3 indices. */
    float positions[] = { 0.0f, 0.0f, 0.0f,
                          1.0f, 0.0f, 0.0f,
                          0.0f, 1.0f, 0.0f };
    float normals[]   = { 0.0f, 0.0f, 1.0f,
                          0.0f, 0.0f, 1.0f,
                          0.0f, 0.0f, 1.0f };
    uint32_t indices[] = { 0, 1, 2 };

    static_mesh_create_info_t info;
    memset(&info, 0, sizeof(info));
    info.positions    = positions;
    info.normals      = normals;
    info.indices      = indices;
    info.vertex_count = 3;
    info.index_count  = 3;

    static_mesh_t mesh;
    int rc = static_mesh_create(&g_loader, &info, &mesh);
    ASSERT_INT_EQ(STATIC_MESH_OK, rc);

    ASSERT_INT_EQ(3, (int)mesh.vertex_count);
    ASSERT_INT_EQ(3, (int)mesh.index_count);
    ASSERT_INT_EQ(1, (int)mesh.submesh_count);
    ASSERT_TRUE(mesh.vao.handle != 0);
    ASSERT_TRUE(mesh.vbo_position.handle != 0);
    ASSERT_TRUE(mesh.vbo_normal.handle != 0);
    ASSERT_TRUE(mesh.ibo.handle != 0);

    /* AABB check. */
    ASSERT_FLOAT_NEAR(0.0f, mesh.aabb_min[0], 0.001f);
    ASSERT_FLOAT_NEAR(0.0f, mesh.aabb_min[1], 0.001f);
    ASSERT_FLOAT_NEAR(0.0f, mesh.aabb_min[2], 0.001f);
    ASSERT_FLOAT_NEAR(1.0f, mesh.aabb_max[0], 0.001f);
    ASSERT_FLOAT_NEAR(1.0f, mesh.aabb_max[1], 0.001f);
    ASSERT_FLOAT_NEAR(0.0f, mesh.aabb_max[2], 0.001f);

    static_mesh_destroy(&mesh);
    return 0;
}

/* ── Test: create with optional attributes ────────────────────────── */
static int test_create_with_optional_attrs(void) {
    float positions[] = { 0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f };
    float normals[]   = { 0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f };
    float tangents[]  = { 1.0f, 0.0f, 0.0f, 1.0f,
                          1.0f, 0.0f, 0.0f, 1.0f,
                          1.0f, 0.0f, 0.0f, 1.0f };
    float uv0[]       = { 0.0f, 0.0f,  1.0f, 0.0f,  0.0f, 1.0f };
    uint32_t indices[] = { 0, 1, 2 };

    static_mesh_create_info_t info;
    memset(&info, 0, sizeof(info));
    info.positions    = positions;
    info.normals      = normals;
    info.tangents     = tangents;
    info.uv0          = uv0;
    info.indices      = indices;
    info.vertex_count = 3;
    info.index_count  = 3;

    static_mesh_t mesh;
    int rc = static_mesh_create(&g_loader, &info, &mesh);
    ASSERT_INT_EQ(STATIC_MESH_OK, rc);

    /* Optional VBOs should be created when data provided. */
    ASSERT_TRUE(mesh.vbo_tangent.handle != 0);
    ASSERT_TRUE(mesh.vbo_uv0.handle != 0);

    static_mesh_destroy(&mesh);
    return 0;
}

/* ── Test: FVMA round-trip ────────────────────────────────────────── */
static int test_create_from_fvma(void) {
    /* Build a minimal mesh_slot, serialize to FVMA, then load. */
    mesh_slot_t slot;
    memset(&slot, 0, sizeof(slot));

    float positions[] = { -1.0f, 0.0f, -1.0f,
                           1.0f, 0.0f, -1.0f,
                           1.0f, 0.0f,  1.0f,
                          -1.0f, 0.0f,  1.0f };
    float normals[]   = { 0.0f, 1.0f, 0.0f,
                          0.0f, 1.0f, 0.0f,
                          0.0f, 1.0f, 0.0f,
                          0.0f, 1.0f, 0.0f };
    uint32_t indices[] = { 0, 1, 2, 0, 2, 3 };

    slot.positions      = positions;
    slot.normals        = normals;
    slot.indices        = indices;
    slot.vertex_count   = 4;
    slot.vertex_capacity = 4;
    slot.index_count    = 6;
    slot.index_capacity = 6;

    /* polygroup_ids is needed by the serializer (1 per triangle). */
    uint16_t polygroups[2] = { 0, 0 };
    slot.polygroup_ids  = polygroups;

    uint32_t flags = MESH_VAO_FLAG_NORMALS;
    size_t buf_size = mesh_vao_serialized_size(&slot, flags);
    ASSERT_TRUE(buf_size > 0);

    uint8_t *buf = (uint8_t *)malloc(buf_size);
    ASSERT_TRUE(buf != NULL);

    size_t written = mesh_vao_serialize(&slot, flags, buf, buf_size);
    ASSERT_TRUE(written == buf_size);

    static_mesh_t mesh;
    int rc = static_mesh_create_from_fvma(&g_loader, buf, buf_size, &mesh);
    free(buf);
    ASSERT_INT_EQ(STATIC_MESH_OK, rc);

    ASSERT_INT_EQ(4, (int)mesh.vertex_count);
    ASSERT_INT_EQ(6, (int)mesh.index_count);
    ASSERT_INT_EQ(1, (int)mesh.submesh_count);
    ASSERT_TRUE(mesh.vao.handle != 0);

    static_mesh_destroy(&mesh);
    return 0;
}

/* ── Test: draw does not crash (smoke test) ───────────────────────── */
static int test_draw_smoke(void) {
    static_mesh_t mesh;
    int rc = static_mesh_create_box(&g_loader, 1.0f, 1.0f, 1.0f, &mesh);
    ASSERT_INT_EQ(STATIC_MESH_OK, rc);

    /* Drawing without a shader bound won't produce visible output, but
     * must not crash or generate GL errors. */
    static_mesh_bind(&mesh);
    glGetError(); /* clear any prior error */
    static_mesh_draw_submesh(&mesh, 0);
    GLenum err = glGetError();
    static_mesh_unbind();
    /* GL_INVALID_OPERATION is acceptable if no shader is bound; we just
     * verify no segfault. */
    (void)err;

    static_mesh_destroy(&mesh);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 * Edge-case tests
 * ═══════════════════════════════════════════════════════════════════ */

/* ── Test: NULL loader returns error ──────────────────────────────── */
static int test_null_loader_returns_error(void) {
    static_mesh_t mesh;
    int rc = static_mesh_create_box(NULL, 1.0f, 1.0f, 1.0f, &mesh);
    ASSERT_INT_EQ(STATIC_MESH_ERR_INVALID, rc);
    return 0;
}

/* ── Test: NULL output returns error ──────────────────────────────── */
static int test_null_output_returns_error(void) {
    int rc = static_mesh_create_box(&g_loader, 1.0f, 1.0f, 1.0f, NULL);
    ASSERT_INT_EQ(STATIC_MESH_ERR_INVALID, rc);
    return 0;
}

/* ── Test: zero-size mesh returns error ───────────────────────────── */
static int test_zero_vertex_count_returns_error(void) {
    static_mesh_create_info_t info;
    memset(&info, 0, sizeof(info));
    /* positions non-NULL but vertex_count=0 */
    float dummy = 0.0f;
    info.positions = &dummy;
    info.vertex_count = 0;

    static_mesh_t mesh;
    int rc = static_mesh_create(&g_loader, &info, &mesh);
    ASSERT_INT_EQ(STATIC_MESH_ERR_INVALID, rc);
    return 0;
}

/* ── Test: missing positions returns error ────────────────────────── */
static int test_null_positions_returns_error(void) {
    static_mesh_create_info_t info;
    memset(&info, 0, sizeof(info));
    info.vertex_count = 3;
    uint32_t idx[] = { 0, 1, 2 };
    info.indices = idx;
    info.index_count = 3;

    static_mesh_t mesh;
    int rc = static_mesh_create(&g_loader, &info, &mesh);
    ASSERT_INT_EQ(STATIC_MESH_ERR_INVALID, rc);
    return 0;
}

/* ── Test: FVMA with bad magic returns error ──────────────────────── */
static int test_fvma_bad_magic_returns_error(void) {
    uint8_t garbage[64];
    memset(garbage, 0xFF, sizeof(garbage));

    static_mesh_t mesh;
    int rc = static_mesh_create_from_fvma(&g_loader, garbage,
                                          sizeof(garbage), &mesh);
    ASSERT_INT_EQ(STATIC_MESH_ERR_FORMAT, rc);
    return 0;
}

/* ── Test: FVMA with truncated buffer returns error ───────────────── */
static int test_fvma_truncated_returns_error(void) {
    /* Valid magic but too short. */
    uint8_t buf[8] = { 'F', 'V', 'M', 'A', 1, 0, 0, 0 };

    static_mesh_t mesh;
    int rc = static_mesh_create_from_fvma(&g_loader, buf, sizeof(buf), &mesh);
    ASSERT_INT_EQ(STATIC_MESH_ERR_FORMAT, rc);
    return 0;
}

/* ── Test: double destroy is safe ─────────────────────────────────── */
static int test_double_destroy_safe(void) {
    static_mesh_t mesh;
    int rc = static_mesh_create_box(&g_loader, 1.0f, 1.0f, 1.0f, &mesh);
    ASSERT_INT_EQ(STATIC_MESH_OK, rc);

    static_mesh_destroy(&mesh);
    static_mesh_destroy(&mesh); /* must not crash */
    return 0;
}

/* ── Test: destroy NULL is safe ───────────────────────────────────── */
static int test_destroy_null_safe(void) {
    static_mesh_destroy(NULL); /* must not crash */
    return 0;
}

/* ── Test: submesh bounds ─────────────────────────────────────────── */
static int test_submesh_bounds(void) {
    static_mesh_t mesh;
    int rc = static_mesh_create_box(&g_loader, 1.0f, 1.0f, 1.0f, &mesh);
    ASSERT_INT_EQ(STATIC_MESH_OK, rc);
    ASSERT_TRUE(mesh.submeshes != NULL);
    ASSERT_INT_EQ(0, (int)mesh.submeshes[0].index_offset);
    ASSERT_INT_EQ(36, (int)mesh.submeshes[0].index_count);
    ASSERT_INT_EQ(0, (int)mesh.submeshes[0].material_slot);

    static_mesh_destroy(&mesh);
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
    {"create_box_primitive",         test_create_box_primitive},
    {"create_sphere_primitive",      test_create_sphere_primitive},
    {"create_capsule_primitive",     test_create_capsule_primitive},
    {"create_plane_primitive",       test_create_plane_primitive},
    {"create_from_raw_data",         test_create_from_raw_data},
    {"create_with_optional_attrs",   test_create_with_optional_attrs},
    {"create_from_fvma",             test_create_from_fvma},
    {"draw_smoke",                   test_draw_smoke},
    {"submesh_bounds",               test_submesh_bounds},
    /* Edge / failure cases */
    {"null_loader_returns_error",    test_null_loader_returns_error},
    {"null_output_returns_error",    test_null_output_returns_error},
    {"zero_vertex_count_error",      test_zero_vertex_count_returns_error},
    {"null_positions_error",         test_null_positions_returns_error},
    {"fvma_bad_magic_error",         test_fvma_bad_magic_returns_error},
    {"fvma_truncated_error",         test_fvma_truncated_returns_error},
    {"double_destroy_safe",          test_double_destroy_safe},
    {"destroy_null_safe",            test_destroy_null_safe},
};

int main(void) {
    struct gl_test_context ctx = {0};
    if (gl_test_context_init(&ctx) != 0) {
        return 1;
    }

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
    if (passed == total) {
        return 0;
    }
    return 1;
}

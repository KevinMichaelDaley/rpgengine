/**
 * @file p004_gltf_loader_tests.c
 * @brief Unit tests for glTF/GLB loader (gltf_loader.h).
 *
 * Tests parse, query, and mesh creation against asset_src/humanoid.glb.
 * Requires an OpenGL 3.3 context (SDL2 + GLAD).
 */

#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/gltf/gltf_loader.h"
#include "ferrum/renderer/mesh/static_mesh.h"
#include "ferrum/renderer/mesh/skeletal_mesh.h"

/* ── Test macros ──────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "  ASSERT_TRUE failed: %s:%d: %s\n", \
                __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

#define ASSERT_EQ(exp, act) do { \
    if ((exp) != (act)) { \
        fprintf(stderr, "  ASSERT_EQ failed: %s:%d: expected %d got %d\n", \
                __FILE__, __LINE__, (int)(exp), (int)(act)); \
        return 1; \
    } \
} while (0)

/* ── GL context setup ─────────────────────────────────────────────── */

static SDL_Window   *g_window;
static SDL_GLContext  g_gl_ctx;
static gl_loader_t   g_loader;

static void *sdl_get_proc_(const char *name, void *ud) {
    (void)ud;
    return SDL_GL_GetProcAddress(name);
}

static int init_gl_(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { return -1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    g_window = SDL_CreateWindow("gltf_tests", 0, 0, 64, 64,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!g_window) { SDL_Quit(); return -1; }
    g_gl_ctx = SDL_GL_CreateContext(g_window);
    if (!g_gl_ctx) { SDL_DestroyWindow(g_window); SDL_Quit(); return -1; }
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        SDL_GL_DeleteContext(g_gl_ctx);
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return -1;
    }
    (void)glGetError();
    g_loader.get_proc_address = sdl_get_proc_;
    g_loader.user_data = NULL;
    return 0;
}

static void cleanup_gl_(void) {
    SDL_GL_DeleteContext(g_gl_ctx);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
}

/* ═══════════════════════════════════════════════════════════════════
 *  HAPPY PATH
 * ═══════════════════════════════════════════════════════════════════ */

/* Load humanoid.glb successfully. */
static int test_load_glb(void) {
    gltf_scene_t *scene = NULL;
    gltf_status_t rc = gltf_scene_load("asset_src/humanoid.glb", &scene);
    ASSERT_EQ(GLTF_OK, rc);
    ASSERT_TRUE(scene != NULL);
    gltf_scene_destroy(scene);
    return 0;
}

/* Mesh count matches expected (84 meshes in humanoid.glb). */
static int test_mesh_count(void) {
    gltf_scene_t *scene = NULL;
    gltf_scene_load("asset_src/humanoid.glb", &scene);
    uint32_t count = gltf_scene_mesh_count(scene);
    ASSERT_TRUE(count == 84);
    gltf_scene_destroy(scene);
    return 0;
}

/* Query mesh info for a skinned mesh. */
static int test_mesh_info_skinned(void) {
    gltf_scene_t *scene = NULL;
    gltf_scene_load("asset_src/humanoid.glb", &scene);

    /* mesh[76] = "exports.002" — multi-primitive skinned mesh (merged). */
    gltf_mesh_info_t info;
    gltf_status_t rc = gltf_scene_mesh_info(scene, 76, &info);
    ASSERT_EQ(GLTF_OK, rc);
    ASSERT_TRUE(info.vertex_count == 22257);
    ASSERT_TRUE(info.index_count == 97944);
    ASSERT_TRUE(info.is_skinned != 0);
    ASSERT_TRUE(strlen(info.name) > 0);

    gltf_scene_destroy(scene);
    return 0;
}

/* Query mesh info for a non-skinned mesh. */
static int test_mesh_info_static(void) {
    gltf_scene_t *scene = NULL;
    gltf_scene_load("asset_src/humanoid.glb", &scene);

    /* mesh[0] = "Cylinder" — not skinned. */
    gltf_mesh_info_t info;
    gltf_status_t rc = gltf_scene_mesh_info(scene, 0, &info);
    ASSERT_EQ(GLTF_OK, rc);
    ASSERT_TRUE(info.vertex_count > 0);
    ASSERT_TRUE(info.is_skinned == 0);

    gltf_scene_destroy(scene);
    return 0;
}

/* Joint count from the first skin. */
static int test_joint_count(void) {
    gltf_scene_t *scene = NULL;
    gltf_scene_load("asset_src/humanoid.glb", &scene);
    uint32_t joints = gltf_scene_joint_count(scene);
    ASSERT_TRUE(joints == 333);
    gltf_scene_destroy(scene);
    return 0;
}

/* Create a static mesh from a non-skinned glTF mesh. */
static int test_create_static_mesh(void) {
    gltf_scene_t *scene = NULL;
    gltf_scene_load("asset_src/humanoid.glb", &scene);

    static_mesh_t mesh;
    gltf_status_t rc = gltf_scene_create_static_mesh(scene, 0, &g_loader, &mesh);
    ASSERT_EQ(GLTF_OK, rc);
    ASSERT_TRUE(mesh.vertex_count > 0);
    ASSERT_TRUE(mesh.index_count > 0);
    ASSERT_TRUE(mesh.vao.handle != 0);
    ASSERT_TRUE(glGetError() == GL_NO_ERROR);

    static_mesh_destroy(&mesh);
    gltf_scene_destroy(scene);
    return 0;
}

/* Create a skeletal mesh from a skinned glTF mesh. */
static int test_create_skeletal_mesh(void) {
    gltf_scene_t *scene = NULL;
    gltf_scene_load("asset_src/humanoid.glb", &scene);

    /* Use mesh[76] "exports.002" — largest skinned mesh. */
    skeletal_mesh_t mesh;
    gltf_status_t rc = gltf_scene_create_skeletal_mesh(scene, 76, &g_loader, &mesh);
    ASSERT_EQ(GLTF_OK, rc);
    ASSERT_TRUE(mesh.base.vertex_count == 22257);
    ASSERT_TRUE(mesh.base.index_count == 97944);
    ASSERT_TRUE(mesh.bone_count > 0);
    ASSERT_TRUE(mesh.inv_bind_matrices != NULL);
    ASSERT_TRUE(mesh.vbo_bone_weights.handle != 0);
    ASSERT_TRUE(mesh.vbo_bone_indices.handle != 0);
    ASSERT_TRUE(glGetError() == GL_NO_ERROR);

    skeletal_mesh_destroy(&mesh);
    gltf_scene_destroy(scene);
    return 0;
}

/* Creating skeletal mesh from non-skinned mesh should fail. */
static int test_create_skeletal_from_static_fails(void) {
    gltf_scene_t *scene = NULL;
    gltf_scene_load("asset_src/humanoid.glb", &scene);

    skeletal_mesh_t mesh;
    gltf_status_t rc = gltf_scene_create_skeletal_mesh(scene, 0, &g_loader, &mesh);
    ASSERT_TRUE(rc != GLTF_OK);

    gltf_scene_destroy(scene);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  EDGE CASES
 * ═══════════════════════════════════════════════════════════════════ */

/* Mesh info out of range. */
static int test_mesh_info_out_of_range(void) {
    gltf_scene_t *scene = NULL;
    gltf_scene_load("asset_src/humanoid.glb", &scene);

    gltf_mesh_info_t info;
    gltf_status_t rc = gltf_scene_mesh_info(scene, 9999, &info);
    ASSERT_EQ(GLTF_ERR_INVALID, rc);

    gltf_scene_destroy(scene);
    return 0;
}

/* Destroy NULL scene is safe. */
static int test_destroy_null(void) {
    gltf_scene_destroy(NULL);
    return 0;
}

/* Mesh count of NULL scene returns 0. */
static int test_mesh_count_null(void) {
    ASSERT_TRUE(gltf_scene_mesh_count(NULL) == 0);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  BIND POSE COMPUTATION
 * ═══════════════════════════════════════════════════════════════════ */

/**
 * Bind pose matrices should be non-identity when the skeleton has
 * actual joint transforms and inverse bind matrices.
 */
static int test_compute_bind_pose(void) {
    gltf_scene_t *scene = NULL;
    gltf_scene_load("asset_src/humanoid.glb", &scene);

    uint32_t joint_count = gltf_scene_joint_count(scene);
    ASSERT_TRUE(joint_count == 333);

    float *bind_pose = (float *)malloc((size_t)joint_count * 16 * sizeof(float));
    ASSERT_TRUE(bind_pose != NULL);

    gltf_status_t rc = gltf_scene_compute_bind_pose(scene, bind_pose,
                                                     joint_count);
    ASSERT_EQ(GLTF_OK, rc);

    /* At least some bones should NOT be identity — the humanoid has
       non-trivial joint transforms and inverse bind matrices. */
    int non_identity = 0;
    for (uint32_t j = 0; j < joint_count; ++j) {
        float *m = &bind_pose[j * 16];
        /* Quick check: if m[12],m[13],m[14] (translation) are all 0
           AND diagonal is all 1, it's identity. */
        int is_ident = (fabsf(m[0] - 1.0f) < 1e-5f &&
                        fabsf(m[5] - 1.0f) < 1e-5f &&
                        fabsf(m[10] - 1.0f) < 1e-5f &&
                        fabsf(m[15] - 1.0f) < 1e-5f &&
                        fabsf(m[12]) < 1e-5f &&
                        fabsf(m[13]) < 1e-5f &&
                        fabsf(m[14]) < 1e-5f);
        if (!is_ident) non_identity++;
    }
    ASSERT_TRUE(non_identity > 0);

    free(bind_pose);
    gltf_scene_destroy(scene);
    return 0;
}

/* Bind pose with NULL scene should fail. */
static int test_compute_bind_pose_null(void) {
    float dummy[16];
    gltf_status_t rc = gltf_scene_compute_bind_pose(NULL, dummy, 1);
    ASSERT_EQ(GLTF_ERR_INVALID, rc);
    return 0;
}

/* Bind pose with insufficient buffer capacity should fail. */
static int test_compute_bind_pose_small_buffer(void) {
    gltf_scene_t *scene = NULL;
    gltf_scene_load("asset_src/humanoid.glb", &scene);

    float buf[16]; /* only 1 bone, need 333 */
    gltf_status_t rc = gltf_scene_compute_bind_pose(scene, buf, 1);
    ASSERT_EQ(GLTF_ERR_INVALID, rc);

    gltf_scene_destroy(scene);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  FAILURE MODES
 * ═══════════════════════════════════════════════════════════════════ */

/* Load nonexistent file. */
static int test_load_missing_file(void) {
    gltf_scene_t *scene = NULL;
    gltf_status_t rc = gltf_scene_load("nonexistent.glb", &scene);
    ASSERT_EQ(GLTF_ERR_IO, rc);
    ASSERT_TRUE(scene == NULL);
    return 0;
}

/* Load with NULL path. */
static int test_load_null_path(void) {
    gltf_scene_t *scene = NULL;
    gltf_status_t rc = gltf_scene_load(NULL, &scene);
    ASSERT_EQ(GLTF_ERR_INVALID, rc);
    return 0;
}

/* Load with NULL output. */
static int test_load_null_out(void) {
    gltf_status_t rc = gltf_scene_load("asset_src/humanoid.glb", NULL);
    ASSERT_EQ(GLTF_ERR_INVALID, rc);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Test runner
 * ═══════════════════════════════════════════════════════════════════ */

typedef struct { const char *name; int (*fn)(void); } test_entry_t;

static const test_entry_t TESTS[] = {
    /* Happy path */
    {"load_glb",                     test_load_glb},
    {"mesh_count",                   test_mesh_count},
    {"mesh_info_skinned",            test_mesh_info_skinned},
    {"mesh_info_static",             test_mesh_info_static},
    {"joint_count",                  test_joint_count},
    {"create_static_mesh",           test_create_static_mesh},
    {"create_skeletal_mesh",         test_create_skeletal_mesh},
    {"create_skeletal_from_static",  test_create_skeletal_from_static_fails},
    /* Edge cases */
    {"mesh_info_out_of_range",       test_mesh_info_out_of_range},
    {"destroy_null",                 test_destroy_null},
    {"mesh_count_null",              test_mesh_count_null},
    /* Bind pose */
    {"compute_bind_pose",            test_compute_bind_pose},
    {"compute_bind_pose_null",       test_compute_bind_pose_null},
    {"compute_bind_pose_small_buf",  test_compute_bind_pose_small_buffer},
    /* Failure modes */
    {"load_missing_file",            test_load_missing_file},
    {"load_null_path",               test_load_null_path},
    {"load_null_out",                test_load_null_out},
};

int main(void) {
    if (init_gl_() != 0) {
        fprintf(stderr, "GL init failed\n");
        return 1;
    }

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

    cleanup_gl_();
    return (passed == total) ? 0 : 1;
}

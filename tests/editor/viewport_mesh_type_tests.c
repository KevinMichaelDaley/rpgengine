/**
 * @file viewport_mesh_type_tests.c
 * @brief Tests for per-entity mesh type tracking and skeletal mesh integration.
 *
 * Validates the viewport_mesh_type_t enum, per-entity mesh type array
 * behavior, and the safety check preventing skeletal→static downgrade.
 */

#include "ferrum/editor/viewport/viewport_mesh_type.h"
#include "ferrum/editor/mesh/mesh_vao_format.h"

#include <stdio.h>
#include <string.h>

static int g_pass, g_fail;

#define ASSERT(cond) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; \
           fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/* ---- Tests ---- */

/** Enum values are distinct and correctly ordered. */
static void test_enum_values(void) {
    ASSERT(VIEWPORT_MESH_NONE == 0);
    ASSERT(VIEWPORT_MESH_STATIC == 1);
    ASSERT(VIEWPORT_MESH_SKELETAL == 2);
    ASSERT(VIEWPORT_MESH_NONE != VIEWPORT_MESH_STATIC);
    ASSERT(VIEWPORT_MESH_STATIC != VIEWPORT_MESH_SKELETAL);
}

/** MESH_VAO_FLAG_BONES exists and is a power of 2. */
static void test_bones_flag(void) {
    ASSERT(MESH_VAO_FLAG_BONES != 0);
    /* Must be a single bit (power of 2). */
    ASSERT((MESH_VAO_FLAG_BONES & (MESH_VAO_FLAG_BONES - 1)) == 0);
}

/** viewport_mesh_type_can_upgrade validates allowed transitions. */
static void test_can_upgrade(void) {
    /* NONE → STATIC: allowed */
    ASSERT(viewport_mesh_type_can_upgrade(VIEWPORT_MESH_NONE,
                                           VIEWPORT_MESH_STATIC));
    /* NONE → SKELETAL: allowed */
    ASSERT(viewport_mesh_type_can_upgrade(VIEWPORT_MESH_NONE,
                                           VIEWPORT_MESH_SKELETAL));
    /* STATIC → SKELETAL: allowed (skeleton assignment) */
    ASSERT(viewport_mesh_type_can_upgrade(VIEWPORT_MESH_STATIC,
                                           VIEWPORT_MESH_SKELETAL));
    /* SKELETAL → STATIC: FORBIDDEN (lossy, destructive) */
    ASSERT(!viewport_mesh_type_can_upgrade(VIEWPORT_MESH_SKELETAL,
                                            VIEWPORT_MESH_STATIC));
    /* SKELETAL → NONE: FORBIDDEN (would lose data) */
    ASSERT(!viewport_mesh_type_can_upgrade(VIEWPORT_MESH_SKELETAL,
                                            VIEWPORT_MESH_NONE));
    /* Same → Same: allowed (no-op) */
    ASSERT(viewport_mesh_type_can_upgrade(VIEWPORT_MESH_STATIC,
                                           VIEWPORT_MESH_STATIC));
    ASSERT(viewport_mesh_type_can_upgrade(VIEWPORT_MESH_SKELETAL,
                                           VIEWPORT_MESH_SKELETAL));
    /* STATIC → NONE: allowed (unloading mesh) */
    ASSERT(viewport_mesh_type_can_upgrade(VIEWPORT_MESH_STATIC,
                                           VIEWPORT_MESH_NONE));
}

/* ---- Main ---- */

int main(void) {
    printf("viewport_mesh_type_tests:\n");

    test_enum_values();
    test_bones_flag();
    test_can_upgrade();

    printf("viewport_mesh_type_tests: %d passed, %d failed\n",
           g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}

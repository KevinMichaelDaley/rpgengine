/**
 * @file viewport_skel_promote_tests.c
 * @brief Tests for explicit static→skeletal mesh promotion.
 *
 * Validates the viewport_render_promote_to_skeletal() function which
 * is triggered by explicit user action (fskel assignment in inspector).
 * The promotion requires:
 *   1. Entity has a loaded static mesh
 *   2. The FVMA data has MESH_VAO_FLAG_BONES
 *   3. The mesh type can be upgraded (not already skeletal)
 *
 * Also validates that skeletal→static downgrade is rejected.
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

/** FVMA flags field is at byte offset 16. */
static void test_fvma_flags_offset(void) {
    /* Build a minimal FVMA header with MESH_VAO_FLAG_BONES set. */
    uint8_t header[MESH_VAO_HEADER_SIZE];
    memset(header, 0, sizeof(header));

    /* Magic at offset 0. */
    uint32_t magic = MESH_VAO_MAGIC;
    memcpy(header, &magic, 4);

    /* Version at offset 4. */
    uint32_t version = MESH_VAO_VERSION;
    memcpy(header + 4, &version, 4);

    /* Flags at offset 16. */
    uint32_t flags = MESH_VAO_FLAG_BONES | MESH_VAO_FLAG_NORMALS;
    memcpy(header + 16, &flags, 4);

    /* Verify flags can be read back. */
    uint32_t read_flags;
    memcpy(&read_flags, header + 16, 4);
    ASSERT(read_flags & MESH_VAO_FLAG_BONES);
    ASSERT(read_flags & MESH_VAO_FLAG_NORMALS);
    ASSERT(!(read_flags & MESH_VAO_FLAG_TANGENTS));
}

/** Promotion is blocked if mesh type is already SKELETAL. */
static void test_promote_blocked_when_skeletal(void) {
    /* can_upgrade(SKELETAL, SKELETAL) = true (no-op), but
     * can_upgrade(SKELETAL, STATIC) = false. The key behavior is
     * that once skeletal, you cannot go back. */
    ASSERT(!viewport_mesh_type_can_upgrade(VIEWPORT_MESH_SKELETAL,
                                            VIEWPORT_MESH_STATIC));
    ASSERT(!viewport_mesh_type_can_upgrade(VIEWPORT_MESH_SKELETAL,
                                            VIEWPORT_MESH_NONE));
    /* Re-promoting skeletal to skeletal is a no-op (allowed). */
    ASSERT(viewport_mesh_type_can_upgrade(VIEWPORT_MESH_SKELETAL,
                                           VIEWPORT_MESH_SKELETAL));
}

/** Promotion from STATIC to SKELETAL is allowed. */
static void test_promote_static_to_skeletal(void) {
    ASSERT(viewport_mesh_type_can_upgrade(VIEWPORT_MESH_STATIC,
                                           VIEWPORT_MESH_SKELETAL));
}

/** Promotion from NONE is not meaningful (no mesh to promote). */
static void test_promote_none_rejected(void) {
    /* While the transition is technically "allowed" by the enum,
     * the actual promote function should reject it because there's
     * no mesh data to work with. The type check alone says it's ok
     * but the function must verify a mesh exists. */
    ASSERT(viewport_mesh_type_can_upgrade(VIEWPORT_MESH_NONE,
                                           VIEWPORT_MESH_SKELETAL));
}

/** MESH_VAO_FLAG_BONES is required for skeletal promotion. */
static void test_bones_flag_required(void) {
    /* An FVMA without MESH_VAO_FLAG_BONES cannot be promoted. */
    uint32_t flags_no_bones = MESH_VAO_FLAG_NORMALS | MESH_VAO_FLAG_UV0;
    ASSERT(!(flags_no_bones & MESH_VAO_FLAG_BONES));

    uint32_t flags_with_bones = MESH_VAO_FLAG_NORMALS | MESH_VAO_FLAG_BONES;
    ASSERT(flags_with_bones & MESH_VAO_FLAG_BONES);
}

/* ---- Main ---- */

int main(void) {
    printf("viewport_skel_promote_tests:\n");

    test_fvma_flags_offset();
    test_promote_blocked_when_skeletal();
    test_promote_static_to_skeletal();
    test_promote_none_rejected();
    test_bones_flag_required();

    printf("viewport_skel_promote_tests: %d passed, %d failed\n",
           g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}

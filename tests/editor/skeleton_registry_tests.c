/**
 * @file skeleton_registry_tests.c
 * @brief Tests for editor skeleton registry and load_skeleton command.
 */

#include "ferrum/editor/edit_skeleton_registry.h"
#include "ferrum/animation/constraint_params.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_pass, s_fail;
#define TEST(name) do { printf("RUN  " #name "\n"); } while(0)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL " #cond " at %s:%d\n", __FILE__, __LINE__); \
        s_fail++; return; \
    } \
} while(0)
#define OK(name) do { printf("OK   " #name "\n"); s_pass++; } while(0)

/** Helper: create a minimal skeleton with the given joint count. */
static bool make_skel_(skeleton_def_t *skel, uint32_t joints) {
    memset(skel, 0, sizeof(*skel));
    return skeleton_def_init(skel, joints, 0);
}

/* ---- Tests ---- */

static void test_init_destroy(void) {
    TEST(test_init_destroy);
    edit_skeleton_registry_t reg;
    ASSERT(edit_skeleton_registry_init(&reg, 8));
    ASSERT(reg.capacity == 8);
    ASSERT(reg.count == 0);
    edit_skeleton_registry_destroy(&reg);
    ASSERT(reg.capacity == 0);
    ASSERT(reg.count == 0);
    OK(test_init_destroy);
}

static void test_init_zero_capacity(void) {
    TEST(test_init_zero_capacity);
    edit_skeleton_registry_t reg;
    ASSERT(!edit_skeleton_registry_init(&reg, 0));
    OK(test_init_zero_capacity);
}

static void test_init_null(void) {
    TEST(test_init_null);
    ASSERT(!edit_skeleton_registry_init(NULL, 8));
    OK(test_init_null);
}

static void test_add_and_get(void) {
    TEST(test_add_and_get);
    edit_skeleton_registry_t reg;
    ASSERT(edit_skeleton_registry_init(&reg, 4));

    skeleton_def_t skel;
    ASSERT(make_skel_(&skel, 2));
    strncpy(skel.joint_names[0], "root", SKELETON_JOINT_NAME_MAX - 1);
    strncpy(skel.joint_names[1], "spine", SKELETON_JOINT_NAME_MAX - 1);

    /* add takes ownership — skel is zeroed after. */
    uint32_t idx = edit_skeleton_registry_add(&reg, "humanoid.fskel", &skel,
                                               NULL, 0);
    ASSERT(idx != UINT32_MAX);
    ASSERT(reg.count == 1);
    /* skel should be zeroed (ownership transferred). */
    ASSERT(skel.joint_count == 0);

    const edit_skeleton_entry_t *entry =
        edit_skeleton_registry_get(&reg, "humanoid.fskel");
    ASSERT(entry != NULL);
    ASSERT(entry->skel.joint_count == 2);
    ASSERT(strcmp(entry->path, "humanoid.fskel") == 0);
    ASSERT(strcmp(entry->skel.joint_names[0], "root") == 0);

    edit_skeleton_registry_destroy(&reg);
    OK(test_add_and_get);
}

static void test_get_nonexistent(void) {
    TEST(test_get_nonexistent);
    edit_skeleton_registry_t reg;
    ASSERT(edit_skeleton_registry_init(&reg, 4));

    ASSERT(edit_skeleton_registry_get(&reg, "nope.fskel") == NULL);

    edit_skeleton_registry_destroy(&reg);
    OK(test_get_nonexistent);
}

static void test_add_duplicate_path(void) {
    TEST(test_add_duplicate_path);
    edit_skeleton_registry_t reg;
    ASSERT(edit_skeleton_registry_init(&reg, 4));

    skeleton_def_t skel1;
    ASSERT(make_skel_(&skel1, 1));
    uint32_t idx1 = edit_skeleton_registry_add(&reg, "a.fskel", &skel1,
                                                NULL, 0);
    ASSERT(idx1 != UINT32_MAX);

    /* Adding same path again with a new skeleton should replace. */
    skeleton_def_t skel2;
    ASSERT(make_skel_(&skel2, 3));
    uint32_t idx2 = edit_skeleton_registry_add(&reg, "a.fskel", &skel2,
                                                NULL, 0);
    ASSERT(idx2 == idx1);
    ASSERT(reg.count == 1);

    /* Verify replacement took effect. */
    const edit_skeleton_entry_t *entry =
        edit_skeleton_registry_get(&reg, "a.fskel");
    ASSERT(entry != NULL);
    ASSERT(entry->skel.joint_count == 3);

    edit_skeleton_registry_destroy(&reg);
    OK(test_add_duplicate_path);
}

static void test_add_full(void) {
    TEST(test_add_full);
    edit_skeleton_registry_t reg;
    ASSERT(edit_skeleton_registry_init(&reg, 2));

    skeleton_def_t skel_a;
    ASSERT(make_skel_(&skel_a, 1));
    ASSERT(edit_skeleton_registry_add(&reg, "a.fskel", &skel_a, NULL, 0)
           != UINT32_MAX);

    skeleton_def_t skel_b;
    ASSERT(make_skel_(&skel_b, 1));
    ASSERT(edit_skeleton_registry_add(&reg, "b.fskel", &skel_b, NULL, 0)
           != UINT32_MAX);

    /* Full — should fail. */
    skeleton_def_t skel_c;
    ASSERT(make_skel_(&skel_c, 1));
    ASSERT(edit_skeleton_registry_add(&reg, "c.fskel", &skel_c, NULL, 0)
           == UINT32_MAX);

    /* skel_c was not consumed, must clean up. */
    skeleton_def_destroy(&skel_c);
    edit_skeleton_registry_destroy(&reg);
    OK(test_add_full);
}

static void test_ibm_storage(void) {
    TEST(test_ibm_storage);
    edit_skeleton_registry_t reg;
    ASSERT(edit_skeleton_registry_init(&reg, 4));

    skeleton_def_t skel;
    ASSERT(make_skel_(&skel, 1));

    /* Heap-allocate IBM since registry takes ownership. */
    mat4_t *ibm = (mat4_t *)malloc(sizeof(mat4_t));
    ASSERT(ibm != NULL);
    memset(ibm, 0, sizeof(*ibm));
    ibm->m[0] = 1.0f;   /* Column-major identity diagonal. */
    ibm->m[5] = 1.0f;
    ibm->m[10] = 1.0f;
    ibm->m[15] = 1.0f;

    uint32_t idx = edit_skeleton_registry_add(&reg, "test.fskel", &skel,
                                               ibm, 1);
    ASSERT(idx != UINT32_MAX);

    const edit_skeleton_entry_t *entry =
        edit_skeleton_registry_get(&reg, "test.fskel");
    ASSERT(entry != NULL);
    ASSERT(entry->ibm_count == 1);
    ASSERT(entry->ibms != NULL);
    ASSERT(entry->ibms[0].m[0] == 1.0f);

    /* ibm ownership was transferred — don't free. */
    edit_skeleton_registry_destroy(&reg);
    OK(test_ibm_storage);
}

static void test_load_from_disk(void) {
    TEST(test_load_from_disk);
    edit_skeleton_registry_t reg;
    ASSERT(edit_skeleton_registry_init(&reg, 4));

    bool ok = edit_skeleton_registry_load(&reg, "asset_src/humanoid.fskel");
    ASSERT(ok);
    ASSERT(reg.count == 1);

    const edit_skeleton_entry_t *entry =
        edit_skeleton_registry_get(&reg, "humanoid.fskel");
    ASSERT(entry != NULL);
    ASSERT(entry->skel.joint_count > 0);

    edit_skeleton_registry_destroy(&reg);
    OK(test_load_from_disk);
}

static void test_load_nonexistent(void) {
    TEST(test_load_nonexistent);
    edit_skeleton_registry_t reg;
    ASSERT(edit_skeleton_registry_init(&reg, 4));

    bool ok = edit_skeleton_registry_load(&reg, "asset_src/nope.fskel");
    ASSERT(!ok);
    ASSERT(reg.count == 0);

    edit_skeleton_registry_destroy(&reg);
    OK(test_load_nonexistent);
}

int main(void) {
    test_init_destroy();
    test_init_zero_capacity();
    test_init_null();
    test_add_and_get();
    test_get_nonexistent();
    test_add_duplicate_path();
    test_add_full();
    test_ibm_storage();
    test_load_from_disk();
    test_load_nonexistent();

    printf("\n%d / %d passed\n", s_pass, s_pass + s_fail);
    return s_fail ? 1 : 0;
}

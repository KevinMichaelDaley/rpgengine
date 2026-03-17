/**
 * @file vm_cache_tests.c
 * @brief Tests for vm_reserve-backed caches (entity mesh, skeleton registry).
 *
 * Verifies that caches use demand-paged virtual memory rather than malloc,
 * supporting large capacities (millions of entities) without physical memory
 * pressure until slots are actually used.
 */

#include "ferrum/memory/vm_alloc.h"
#include "ferrum/editor/edit_skeleton_registry.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/editor/viewport/snap/snap_mesh_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int s_pass, s_fail;
#define TEST(name) do { printf("RUN  " #name "\n"); } while(0)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL " #cond " at %s:%d\n", __FILE__, __LINE__); \
        s_fail++; return; \
    } \
} while(0)
#define OK(name) do { printf("OK   " #name "\n"); s_pass++; } while(0)

/* ---- Skeleton registry: large capacity via vm_reserve ---- */

static void test_skeleton_registry_large_capacity(void) {
    TEST(test_skeleton_registry_large_capacity);

    /* Reserve a registry with 1M capacity — should succeed without
     * allocating 1M * sizeof(entry) physical pages. */
    edit_skeleton_registry_t reg;
    ASSERT(edit_skeleton_registry_init(&reg, 1024 * 1024));
    ASSERT(reg.capacity == 1024 * 1024);
    ASSERT(reg.count == 0);

    /* Add a single skeleton — only touches one slot. */
    skeleton_def_t skel;
    memset(&skel, 0, sizeof(skel));
    ASSERT(skeleton_def_init(&skel, 2, 0));
    strncpy(skel.joint_names[0], "root", SKELETON_JOINT_NAME_MAX - 1);

    uint32_t idx = edit_skeleton_registry_add(&reg, "test.fskel", &skel,
                                               NULL, 0);
    ASSERT(idx != UINT32_MAX);
    ASSERT(reg.count == 1);

    /* Look up the entry. */
    const edit_skeleton_entry_t *entry =
        edit_skeleton_registry_get(&reg, "test.fskel");
    ASSERT(entry != NULL);
    ASSERT(entry->skel.joint_count == 2);

    edit_skeleton_registry_destroy(&reg);
    ASSERT(reg.capacity == 0);
    ASSERT(reg.entries == NULL);
    OK(test_skeleton_registry_large_capacity);
}

static void test_skeleton_registry_zero_after_destroy(void) {
    TEST(test_skeleton_registry_zero_after_destroy);

    edit_skeleton_registry_t reg;
    ASSERT(edit_skeleton_registry_init(&reg, 256));
    edit_skeleton_registry_destroy(&reg);
    ASSERT(reg.entries == NULL);
    ASSERT(reg.capacity == 0);
    ASSERT(reg.count == 0);
    OK(test_skeleton_registry_zero_after_destroy);
}

/* ---- Snap mesh cache: large capacity via vm_reserve ---- */

static void test_snap_cache_large_capacity(void) {
    TEST(test_snap_cache_large_capacity);

    /* Reserve a snap cache with 1M capacity. */
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 1024 * 1024);
    ASSERT(cache.meshes != NULL);
    ASSERT(cache.capacity == 1024 * 1024);

    /* Verify untouched slots are zero (demand-paged zeroing). */
    ASSERT(cache.meshes[0].positions == NULL);
    ASSERT(cache.meshes[0].vertex_count == 0);

    /* Insert at a high index — should only page in nearby region. */
    float pos[9] = {0,0,0, 1,0,0, 0,1,0};
    float nrm[9] = {0,0,1, 0,0,1, 0,0,1};
    uint32_t idx[3] = {0,1,2};
    snap_mesh_cache_insert(&cache, 500000, pos, nrm, idx, 3, 3);

    ASSERT(snap_mesh_cache_has(&cache, 500000));
    const snap_mesh_t *m = snap_mesh_cache_get(&cache, 500000);
    ASSERT(m != NULL);
    ASSERT(m->vertex_count == 3);
    ASSERT(m->index_count == 3);

    snap_mesh_cache_destroy(&cache);
    ASSERT(cache.meshes == NULL);
    OK(test_snap_cache_large_capacity);
}

static void test_snap_cache_zero_capacity(void) {
    TEST(test_snap_cache_zero_capacity);

    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 0);
    ASSERT(cache.meshes == NULL);
    ASSERT(cache.capacity == 0);

    snap_mesh_cache_destroy(&cache);
    OK(test_snap_cache_zero_capacity);
}

/* ---- vm_reserve basic sanity ---- */

static void test_vm_reserve_basic(void) {
    TEST(test_vm_reserve_basic);

    /* Reserve 64 MB — should succeed on any modern system. */
    size_t sz = 64 * 1024 * 1024;
    void *p = vm_reserve(sz);
    ASSERT(p != NULL);

    /* Demand-paged: first access should return zero. */
    uint8_t *bytes = (uint8_t *)p;
    ASSERT(bytes[0] == 0);
    ASSERT(bytes[sz - 1] == 0);

    /* Write near the end — only pages in that region. */
    bytes[sz - 1] = 42;
    ASSERT(bytes[sz - 1] == 42);

    vm_release(p, sz);
    OK(test_vm_reserve_basic);
}

static void test_vm_reserve_zero(void) {
    TEST(test_vm_reserve_zero);
    ASSERT(vm_reserve(0) == NULL);
    OK(test_vm_reserve_zero);
}

int main(void) {
    test_vm_reserve_basic();
    test_vm_reserve_zero();
    test_skeleton_registry_large_capacity();
    test_skeleton_registry_zero_after_destroy();
    test_snap_cache_large_capacity();
    test_snap_cache_zero_capacity();

    printf("\n%d / %d passed\n", s_pass, s_pass + s_fail);
    return s_fail ? 1 : 0;
}

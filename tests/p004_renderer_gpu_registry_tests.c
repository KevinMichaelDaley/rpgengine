/**
 * @file p004_renderer_gpu_registry_tests.c
 * @brief Unit tests for the pool-backed GPU resource registry (no GL).
 */
#include <stdio.h>

#include "ferrum/renderer/resource/gpu_registry.h"

#define ASSERT_TRUE(cond)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "ASSERT failed %s:%d: %s\n", __FILE__, __LINE__,  \
                    #cond);                                                   \
            return 1;                                                         \
        }                                                                     \
    } while (0)

/* Alloc yields a live handle whose descriptor carries the requested kind and a
 * pending (not-yet-created) GPU object. */
static int test_alloc_get(void) {
    gpu_registry_t reg;
    ASSERT_TRUE(gpu_registry_init(&reg, 4) == 0);

    uint64_t h = gpu_registry_alloc(&reg, GPU_RESOURCE_TEXTURE);
    ASSERT_TRUE(h != GPU_HANDLE_INVALID);
    gpu_resource_t *r = gpu_registry_get(&reg, h);
    ASSERT_TRUE(r != NULL);
    ASSERT_TRUE(r->kind == GPU_RESOURCE_TEXTURE);
    ASSERT_TRUE(r->gl_name == 0u);                 /* not created on GPU yet. */
    ASSERT_TRUE(atomic_load(&r->ready) == 0);

    gpu_registry_destroy(&reg);
    return 0;
}

/* Allocations exhaust at capacity; the overflow request returns invalid. */
static int test_capacity(void) {
    gpu_registry_t reg;
    ASSERT_TRUE(gpu_registry_init(&reg, 2) == 0);
    uint64_t a = gpu_registry_alloc(&reg, GPU_RESOURCE_TEXTURE);
    uint64_t b = gpu_registry_alloc(&reg, GPU_RESOURCE_BUFFER);
    uint64_t c = gpu_registry_alloc(&reg, GPU_RESOURCE_SHADOW_TARGET);
    ASSERT_TRUE(a != GPU_HANDLE_INVALID && b != GPU_HANDLE_INVALID);
    ASSERT_TRUE(a != b);
    ASSERT_TRUE(c == GPU_HANDLE_INVALID);          /* pool full. */
    gpu_registry_destroy(&reg);
    return 0;
}

/* Freeing invalidates the old handle; reusing the slot bumps the generation so
 * the stale handle no longer resolves. */
static int test_free_generation(void) {
    gpu_registry_t reg;
    ASSERT_TRUE(gpu_registry_init(&reg, 2) == 0);

    uint64_t h = gpu_registry_alloc(&reg, GPU_RESOURCE_TEXTURE);
    ASSERT_TRUE(gpu_registry_get(&reg, h) != NULL);
    gpu_registry_free(&reg, h);
    ASSERT_TRUE(gpu_registry_get(&reg, h) == NULL); /* freed -> stale. */

    uint64_t h2 = gpu_registry_alloc(&reg, GPU_RESOURCE_BUFFER);
    ASSERT_TRUE(h2 != GPU_HANDLE_INVALID);
    ASSERT_TRUE(h2 != h);                           /* generation differs. */
    ASSERT_TRUE(gpu_registry_get(&reg, h) == NULL); /* old handle still stale. */
    ASSERT_TRUE(gpu_registry_get(&reg, h2) != NULL);
    gpu_registry_destroy(&reg);
    return 0;
}

/* The descriptor is mutable through the handle (the render thread fills gl_name
 * and flips ready once the GPU object exists). */
static int test_descriptor_mutation(void) {
    gpu_registry_t reg;
    ASSERT_TRUE(gpu_registry_init(&reg, 2) == 0);
    uint64_t h = gpu_registry_alloc(&reg, GPU_RESOURCE_TEXTURE);
    gpu_resource_t *r = gpu_registry_get(&reg, h);
    r->gl_name = 42u; r->width = 256u; r->height = 128u;
    atomic_store(&r->ready, 1);
    gpu_resource_t *again = gpu_registry_get(&reg, h);
    ASSERT_TRUE(again == r);
    ASSERT_TRUE(again->gl_name == 42u && again->width == 256u);
    ASSERT_TRUE(atomic_load(&again->ready) == 1);
    gpu_registry_destroy(&reg);
    return 0;
}

/* NULL / invalid handles are safe. */
static int test_null_safe(void) {
    gpu_registry_t reg;
    ASSERT_TRUE(gpu_registry_init(&reg, 2) == 0);
    ASSERT_TRUE(gpu_registry_alloc(NULL, GPU_RESOURCE_TEXTURE) == GPU_HANDLE_INVALID);
    ASSERT_TRUE(gpu_registry_get(&reg, GPU_HANDLE_INVALID) == NULL);
    ASSERT_TRUE(gpu_registry_get(NULL, 0) == NULL);
    gpu_registry_free(NULL, 0);          /* no crash. */
    gpu_registry_free(&reg, GPU_HANDLE_INVALID);
    ASSERT_TRUE(gpu_registry_init(NULL, 4) != 0);
    gpu_registry_destroy(&reg);
    gpu_registry_destroy(NULL);
    return 0;
}

struct tc { const char *name; int (*fn)(void); };
static struct tc TESTS[] = {
    { "alloc_get", test_alloc_get },
    { "capacity", test_capacity },
    { "free_generation", test_free_generation },
    { "descriptor_mutation", test_descriptor_mutation },
    { "null_safe", test_null_safe },
};

int main(void) {
    int failed = 0;
    for (size_t i = 0; i < sizeof(TESTS) / sizeof(TESTS[0]); ++i) {
        printf("RUN  %s\n", TESTS[i].name);
        int r = TESTS[i].fn();
        printf(r == 0 ? "OK   %s\n" : "FAIL %s\n", TESTS[i].name);
        failed += (r != 0);
    }
    printf("%s (%d failed)\n", failed ? "FAILED" : "PASSED", failed);
    return failed ? 1 : 0;
}

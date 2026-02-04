#define _POSIX_C_SOURCE 200112L

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_TRUE(cond)                                                                                \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                                          \
    do {                                                                                                 \
        if ((exp) != (act)) {                                                                            \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n", __FILE__, __LINE__,     \
                    (int)(exp), (int)(act));                                                             \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define RUN_TEST(fn)                                                                                     \
    do {                                                                                                 \
        fprintf(stderr, "RUN %s\n", #fn);                                                               \
        int rc = (fn)();                                                                                 \
        if (rc != 0) {                                                                                   \
            fprintf(stderr, "FAIL %s\n", #fn);                                                          \
            return rc;                                                                                   \
        }                                                                                                \
        fprintf(stderr, "OK %s\n", #fn);                                                                \
    } while (0)

extern _Atomic uint64_t fr_alloc_wrap_malloc_calls;
extern _Atomic uint64_t fr_alloc_wrap_calloc_calls;
extern _Atomic uint64_t fr_alloc_wrap_realloc_calls;
extern _Atomic uint64_t fr_alloc_wrap_free_calls;
extern _Atomic uint64_t fr_alloc_wrap_aligned_alloc_calls;
extern _Atomic uint64_t fr_alloc_wrap_posix_memalign_calls;

static int test_malloc_free_roundtrip(void) {
    uint64_t before_malloc = atomic_load(&fr_alloc_wrap_malloc_calls);
    uint64_t before_free = atomic_load(&fr_alloc_wrap_free_calls);

    void *p = malloc(16);
    ASSERT_TRUE(p != NULL);
    memset(p, 0xA5, 16);
    free(p);

    ASSERT_TRUE(atomic_load(&fr_alloc_wrap_malloc_calls) >= before_malloc + 1u);
    ASSERT_TRUE(atomic_load(&fr_alloc_wrap_free_calls) >= before_free + 1u);
    return 0;
}

static int test_calloc_zeroed(void) {
    uint64_t before = atomic_load(&fr_alloc_wrap_calloc_calls);

    uint8_t *p = (uint8_t *)calloc(32, 1);
    ASSERT_TRUE(p != NULL);
    for (int i = 0; i < 32; ++i) {
        ASSERT_INT_EQ(0, p[i]);
    }
    free(p);

    ASSERT_TRUE(atomic_load(&fr_alloc_wrap_calloc_calls) >= before + 1u);
    return 0;
}

static int test_realloc_preserves_prefix(void) {
    uint64_t before = atomic_load(&fr_alloc_wrap_realloc_calls);

    char *p = (char *)malloc(8);
    ASSERT_TRUE(p != NULL);
    memcpy(p, "Ferrum\0", 7);

    char *q = (char *)realloc(p, 64);
    ASSERT_TRUE(q != NULL);
    ASSERT_TRUE(memcmp(q, "Ferrum\0", 7) == 0);
    free(q);

    ASSERT_TRUE(atomic_load(&fr_alloc_wrap_realloc_calls) >= before + 1u);
    return 0;
}

static int test_free_null_is_noop(void) {
    uint64_t before = atomic_load(&fr_alloc_wrap_free_calls);
    void (*free_fn)(void *) = free;
    free_fn(NULL);
    ASSERT_TRUE(atomic_load(&fr_alloc_wrap_free_calls) >= before + 1u);
    return 0;
}

static int test_aligned_alloc_and_posix_memalign(void) {
    uint64_t before_aligned = atomic_load(&fr_alloc_wrap_aligned_alloc_calls);
    uint64_t before_pmem = atomic_load(&fr_alloc_wrap_posix_memalign_calls);

    void *p = aligned_alloc(16, 64);
    ASSERT_TRUE(p != NULL);
    ASSERT_TRUE(((uintptr_t)p % 16u) == 0u);
    free(p);

    void *q = NULL;
    int rc = posix_memalign(&q, 32, 128);
    ASSERT_INT_EQ(0, rc);
    ASSERT_TRUE(q != NULL);
    ASSERT_TRUE(((uintptr_t)q % 32u) == 0u);
    free(q);

    ASSERT_TRUE(atomic_load(&fr_alloc_wrap_aligned_alloc_calls) >= before_aligned + 1u);
    ASSERT_TRUE(atomic_load(&fr_alloc_wrap_posix_memalign_calls) >= before_pmem + 1u);
    return 0;
}

int main(void) {
    RUN_TEST(test_malloc_free_roundtrip);
    RUN_TEST(test_calloc_zeroed);
    RUN_TEST(test_realloc_preserves_prefix);
    RUN_TEST(test_free_null_is_noop);
    RUN_TEST(test_aligned_alloc_and_posix_memalign);
    return 0;
}

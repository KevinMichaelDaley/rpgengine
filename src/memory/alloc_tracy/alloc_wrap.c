#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdatomic.h>

#ifdef TRACY_ENABLE
#include "tracy/TracyC.h"
#endif

_Atomic uint64_t fr_alloc_wrap_malloc_calls = 0;
_Atomic uint64_t fr_alloc_wrap_calloc_calls = 0;
_Atomic uint64_t fr_alloc_wrap_realloc_calls = 0;
_Atomic uint64_t fr_alloc_wrap_free_calls = 0;
_Atomic uint64_t fr_alloc_wrap_aligned_alloc_calls = 0;
_Atomic uint64_t fr_alloc_wrap_posix_memalign_calls = 0;

void *__real_malloc(size_t size);
void *__real_calloc(size_t nmemb, size_t size);
void *__real_realloc(void *ptr, size_t size);
void __real_free(void *ptr);
void *__real_aligned_alloc(size_t alignment, size_t size);
int __real_posix_memalign(void **memptr, size_t alignment, size_t size);

void *__wrap_malloc(size_t size) {
    atomic_fetch_add_explicit(&fr_alloc_wrap_malloc_calls, 1u, memory_order_relaxed);
#ifdef TRACY_ENABLE
    TracyCZoneN(zone, "ALLOC", true);
#endif
    void *ptr = __real_malloc(size);
#ifdef TRACY_ENABLE
    TracyCZoneEnd(zone);
#endif
    return ptr;
}

void *__wrap_calloc(size_t nmemb, size_t size) {
    atomic_fetch_add_explicit(&fr_alloc_wrap_calloc_calls, 1u, memory_order_relaxed);
#ifdef TRACY_ENABLE
    TracyCZoneN(zone, "CALLOC", true);
#endif
    void *ptr = __real_calloc(nmemb, size);
#ifdef TRACY_ENABLE
    TracyCZoneEnd(zone);
#endif
    return ptr;
}

void *__wrap_realloc(void *ptr, size_t size) {
    atomic_fetch_add_explicit(&fr_alloc_wrap_realloc_calls, 1u, memory_order_relaxed);
#ifdef TRACY_ENABLE
    TracyCZoneN(zone, "REALLOC", true);
#endif
    void *out = __real_realloc(ptr, size);
#ifdef TRACY_ENABLE
    TracyCZoneEnd(zone);
#endif
    return out;
}

void __wrap_free(void *ptr) {
    atomic_fetch_add_explicit(&fr_alloc_wrap_free_calls, 1u, memory_order_relaxed);
#ifdef TRACY_ENABLE
    TracyCZoneN(zone, "FREE", true);
#endif
    __real_free(ptr);
#ifdef TRACY_ENABLE
    TracyCZoneEnd(zone);
#endif
}

void *__wrap_aligned_alloc(size_t alignment, size_t size) {
    atomic_fetch_add_explicit(&fr_alloc_wrap_aligned_alloc_calls, 1u, memory_order_relaxed);
#ifdef TRACY_ENABLE
    TracyCZoneN(zone, "ALIGNED_ALLOC", true);
#endif
    void *ptr = __real_aligned_alloc(alignment, size);
#ifdef TRACY_ENABLE
    TracyCZoneEnd(zone);
#endif
    return ptr;
}

int __wrap_posix_memalign(void **memptr, size_t alignment, size_t size) {
    atomic_fetch_add_explicit(&fr_alloc_wrap_posix_memalign_calls, 1u, memory_order_relaxed);
#ifdef TRACY_ENABLE
    TracyCZoneN(zone, "POSIX_MEMALIGN", true);
#endif
    int rc = __real_posix_memalign(memptr, alignment, size);
#ifdef TRACY_ENABLE
    TracyCZoneEnd(zone);
#endif
    return rc;
}

/**
 * @file vm_alloc.h
 * @brief Cross-platform demand-paged virtual memory reservation.
 *
 * vm_reserve() maps a large virtual address range that is zero-initialized
 * on first access.  Physical pages are only committed by the OS when touched,
 * so reserving e.g. 2 GB of virtual space costs almost nothing until the
 * memory is actually written.
 *
 * Supports POSIX (mmap) and Windows (VirtualAlloc).
 */
#ifndef FERRUM_MEMORY_VM_ALLOC_H
#define FERRUM_MEMORY_VM_ALLOC_H

#include <stddef.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <sys/mman.h>
#endif

/**
 * @brief Reserve @p bytes of zero-initialized virtual memory.
 *
 * Pages are demand-paged: physical RAM is consumed only when first written.
 *
 * @param bytes  Number of bytes to reserve.
 * @return Pointer to the reserved region, or NULL on failure.
 */
static inline void *vm_reserve(size_t bytes) {
    if (bytes == 0) return NULL;
#ifdef _WIN32
    return VirtualAlloc(NULL, bytes, MEM_RESERVE | MEM_COMMIT,
                        PAGE_READWRITE);
#else
    void *p = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (p == MAP_FAILED) ? NULL : p;
#endif
}

/**
 * @brief Release virtual memory previously obtained from vm_reserve().
 *
 * @param ptr    Pointer returned by vm_reserve().
 * @param bytes  Size originally passed to vm_reserve().
 */
static inline void vm_release(void *ptr, size_t bytes) {
    if (!ptr) return;
#ifdef _WIN32
    (void)bytes;
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    munmap(ptr, bytes);
#endif
}

#endif /* FERRUM_MEMORY_VM_ALLOC_H */

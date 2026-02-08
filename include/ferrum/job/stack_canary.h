#ifndef FERRUM_JOB_STACK_CANARY_H
#define FERRUM_JOB_STACK_CANARY_H

/**
 * @file stack_canary.h
 * @brief Manual stack-overflow detection for fiber stacks.
 *
 * When JOB_STACK_CANARY is defined (non-zero), a known pattern is painted
 * at the bottom of each fiber stack (the low-address end, since stacks grow
 * downward on x86-64 and AArch64).  Before every context swap away from a
 * fiber, the pattern is verified.  A corrupted canary indicates the fiber
 * overflowed its stack.
 *
 * Compile with -DJOB_STACK_CANARY=1 to enable (see Makefile STACK_CANARY=1).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/** Number of 8-byte words in the canary region (64 bytes total). */
#define JOB_STACK_CANARY_WORDS 8

/** Repeating 8-byte pattern — chosen to be an obviously invalid pointer
 *  and unlikely to appear in normal data. */
#define JOB_STACK_CANARY_PATTERN ((uint64_t)0xDEADCAFEBAADF00Dull)

#if JOB_STACK_CANARY

/**
 * @brief Paint the canary region at the bottom of a fiber stack.
 *
 * Must be called once after the stack memory is allocated, before the
 * fiber is first scheduled.
 *
 * @param stack_base  Pointer to the lowest address of the stack allocation.
 * @param stack_size  Total stack size in bytes (must be >= canary region).
 */
static inline void job_stack_canary_paint(void *stack_base, size_t stack_size) {
    if (!stack_base || stack_size < JOB_STACK_CANARY_WORDS * sizeof(uint64_t)) {
        return;
    }
    uint64_t *words = (uint64_t *)stack_base;
    for (size_t i = 0; i < JOB_STACK_CANARY_WORDS; ++i) {
        words[i] = JOB_STACK_CANARY_PATTERN;
    }
}

/**
 * @brief Verify the canary region at the bottom of a fiber stack.
 *
 * If any word has been overwritten, prints a diagnostic and aborts.
 * Call this before every context swap away from a fiber.
 *
 * @param stack_base   Pointer to the lowest address of the stack allocation.
 * @param stack_size   Total stack size in bytes.
 * @param fiber_id     Fiber identifier for diagnostics.
 * @param debug_label  Human-readable label (e.g. caller site) for diagnostics.
 */
static inline void job_stack_canary_check(const void *stack_base, size_t stack_size,
                                          uint64_t fiber_id, const char *debug_label) {
    if (!stack_base || stack_size < JOB_STACK_CANARY_WORDS * sizeof(uint64_t)) {
        return;
    }
    const uint64_t *words = (const uint64_t *)stack_base;
    for (size_t i = 0; i < JOB_STACK_CANARY_WORDS; ++i) {
        if (words[i] != JOB_STACK_CANARY_PATTERN) {
            fprintf(stderr,
                    "FATAL: fiber stack canary corrupted! fiber_id=%llu word[%zu]="
                    "0x%016llx expected=0x%016llx at %s\n",
                    (unsigned long long)fiber_id,
                    i,
                    (unsigned long long)words[i],
                    (unsigned long long)JOB_STACK_CANARY_PATTERN,
                    debug_label ? debug_label : "unknown");
            abort();
        }
    }
}

#else /* JOB_STACK_CANARY disabled — compile to nothing */

static inline void job_stack_canary_paint(void *stack_base, size_t stack_size) {
    (void)stack_base; (void)stack_size;
}

static inline void job_stack_canary_check(const void *stack_base, size_t stack_size,
                                          uint64_t fiber_id, const char *debug_label) {
    (void)stack_base; (void)stack_size; (void)fiber_id; (void)debug_label;
}

#endif /* JOB_STACK_CANARY */

#endif /* FERRUM_JOB_STACK_CANARY_H */

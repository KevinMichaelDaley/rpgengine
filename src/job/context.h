#ifndef FERRUM_JOB_CONTEXT_H
#define FERRUM_JOB_CONTEXT_H

#include <stddef.h>
#include <stdint.h>

#if defined(__aarch64__)

typedef struct job_context {
    uint64_t x19;
    uint64_t x20;
    uint64_t x21;
    uint64_t x22;
    uint64_t x23;
    uint64_t x24;
    uint64_t x25;
    uint64_t x26;
    uint64_t x27;
    uint64_t x28;
    uint64_t x29_fp;
    uint64_t x30_lr;
    uint64_t sp;
    uint64_t arg0;

    /* Callee-saved SIMD regs (AAPCS64): v8-v15. Stored as 128-bit each. */
    uint64_t v8[2];
    uint64_t v9[2];
    uint64_t v10[2];
    uint64_t v11[2];
    uint64_t v12[2];
    uint64_t v13[2];
    uint64_t v14[2];
    uint64_t v15[2];
} job_context_t;

void job_context_init(job_context_t *ctx, void (*entry)(uintptr_t), uintptr_t arg0, void *stack, size_t stack_size);
void job_context_swap(job_context_t *from, job_context_t *to);

#elif defined(__arm__)

typedef struct job_context {
    uint32_t r4;
    uint32_t r5;
    uint32_t r6;
    uint32_t r7;
    uint32_t r8;
    uint32_t r9;
    uint32_t r10;
    uint32_t r11;
    uint32_t sp;
    uint32_t lr;
    uint32_t arg0;
} job_context_t;

void job_context_init(job_context_t *ctx, void (*entry)(uintptr_t), uintptr_t arg0, void *stack, size_t stack_size);
void job_context_swap(job_context_t *from, job_context_t *to);

#elif defined(__x86_64__)

typedef struct job_context {
    /* Callee-saved GPRs (SysV ABI). */
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;

    /* Stack pointer and resume address. */
    uint64_t sp;
    uint64_t rip;

    /* Bootstrap fields for newly created contexts. */
    uint64_t entry; /* void (*)(uintptr_t) */
    uint64_t arg0;

    /* FPU/MMX/XMM state.
     * FXSAVE64 saves x87 + MMX + XMM0-15 + MXCSR.
     * Requires 16-byte alignment.
     */
    _Alignas(16) uint8_t fxsave_area[512];

#if defined(__AVX512F__)
    /* AVX-512 state (when compiled with AVX-512 enabled).
     * Save all ZMM0-31 and opmask k0-k7.
     */
    uint64_t k_mask[8];
    _Alignas(64) uint8_t zmm[32][64];
#elif defined(__AVX__)
    /* AVX state (when compiled with AVX enabled).
     * Save YMM0-15 (includes the low XMM parts).
     */
    _Alignas(32) uint8_t ymm[16][32];
#endif
} job_context_t;

void job_context_init(job_context_t *ctx, void (*entry)(uintptr_t), uintptr_t arg0, void *stack, size_t stack_size);
void job_context_swap(job_context_t *from, job_context_t *to);

#else

#include <ucontext.h>

typedef ucontext_t job_context_t;

void job_context_swap(job_context_t *from, job_context_t *to);

#endif

#endif /* FERRUM_JOB_CONTEXT_H */

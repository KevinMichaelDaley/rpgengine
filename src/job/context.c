#include <string.h>

#include "context.h"

#if defined(__aarch64__)

_Static_assert(sizeof(job_context_t) == 240u, "job_context_t size must match asm offsets");

void job_context_init(job_context_t *ctx, void (*entry)(uintptr_t), uintptr_t arg0, void *stack, size_t stack_size) {
    if (!ctx || !entry || !stack || stack_size == 0u) {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));

    uintptr_t sp = (uintptr_t)stack + stack_size;
    sp &= ~(uintptr_t)0xFu; /* AArch64 requires 16-byte stack alignment */

    ctx->sp = (uint64_t)sp;
    ctx->x30_lr = (uint64_t)(uintptr_t)entry;
    ctx->arg0 = (uint64_t)arg0;
}

__asm__(
    ".text\n"
    ".p2align 2\n"
    ".global job_context_swap\n"
    ".type job_context_swap,%function\n"
    "job_context_swap:\n"
    /* x0=from, x1=to */
    "stp x19, x20, [x0, #0]\n"
    "stp x21, x22, [x0, #16]\n"
    "stp x23, x24, [x0, #32]\n"
    "stp x25, x26, [x0, #48]\n"
    "stp x27, x28, [x0, #64]\n"
    "stp x29, x30, [x0, #80]\n"
    "mov x2, sp\n"
    "str x2, [x0, #96]\n"
    /* arg0 at 104 is not written here; it is set by job_context_init for fresh fibers. */

    /* Save callee-saved SIMD regs v8-v15 (q8-q15) starting at offset 112. */
    "stp q8,  q9,  [x0, #112]\n"
    "stp q10, q11, [x0, #144]\n"
    "stp q12, q13, [x0, #176]\n"
    "stp q14, q15, [x0, #208]\n"

    /* Restore GPRs from *to. */
    "ldp x19, x20, [x1, #0]\n"
    "ldp x21, x22, [x1, #16]\n"
    "ldp x23, x24, [x1, #32]\n"
    "ldp x25, x26, [x1, #48]\n"
    "ldp x27, x28, [x1, #64]\n"
    "ldp x29, x30, [x1, #80]\n"
    "ldr x2, [x1, #96]\n"
    "mov sp, x2\n"
    "ldr x0, [x1, #104]\n"

    /* Restore SIMD regs. */
    "ldp q8,  q9,  [x1, #112]\n"
    "ldp q10, q11, [x1, #144]\n"
    "ldp q12, q13, [x1, #176]\n"
    "ldp q14, q15, [x1, #208]\n"

    "ret\n"
    ".size job_context_swap, .-job_context_swap\n");

#elif defined(__arm__)

_Static_assert(sizeof(job_context_t) == 44u, "job_context_t size must match asm offsets");

void job_context_init(job_context_t *ctx, void (*entry)(uintptr_t), uintptr_t arg0, void *stack, size_t stack_size) {
    if (!ctx || !entry || !stack || stack_size == 0u) {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));

    uintptr_t sp = (uintptr_t)stack + stack_size;
    sp &= ~(uintptr_t)0xFu; /* keep conservative alignment */

    ctx->sp = (uint32_t)sp;
    ctx->lr = (uint32_t)(uintptr_t)entry;
    ctx->arg0 = (uint32_t)arg0;
}

__asm__(
    ".text\n"
    ".p2align 2\n"
    ".global job_context_swap\n"
    ".type job_context_swap,%function\n"
    "job_context_swap:\n"
    /* r0=from, r1=to */
    "stmia r0, {r4-r11}\n"
    "str sp, [r0, #32]\n"
    "str lr, [r0, #36]\n"

    "ldmia r1, {r4-r11}\n"
    "ldr sp, [r1, #32]\n"
    "ldr lr, [r1, #36]\n"
    "ldr r0, [r1, #40]\n"
    "bx lr\n"
    ".size job_context_swap, .-job_context_swap\n");

#elif defined(__x86_64__)

/*
 * x86-64 SysV: we preserve callee-saved GPRs plus full FP/MMX/XMM state.
 * If built with AVX/AVX-512 enabled, we also preserve YMM/ZMM + opmask.
 */

#include <stddef.h>

#define JOB_CTX_STR2(x) #x
#define XSTR(x) JOB_CTX_STR2(x)

/*
 * Fixed layout offsets for x86_64 job_context_t.
 * These are validated against the C struct layout via _Static_assert.
 */
#define JOB_CTX_X86_OFF_RBX 0
#define JOB_CTX_X86_OFF_RBP 8
#define JOB_CTX_X86_OFF_R12 16
#define JOB_CTX_X86_OFF_R13 24
#define JOB_CTX_X86_OFF_R14 32
#define JOB_CTX_X86_OFF_R15 40
#define JOB_CTX_X86_OFF_SP 48
#define JOB_CTX_X86_OFF_RIP 56
#define JOB_CTX_X86_OFF_ENTRY 64
#define JOB_CTX_X86_OFF_ARG0 72
#define JOB_CTX_X86_OFF_CANARY 80
#define JOB_CTX_X86_OFF_FXSAVE 96

#if defined(__AVX512F__)
#define JOB_CTX_X86_OFF_KMASK 608
#define JOB_CTX_X86_OFF_ZMM 720
#elif defined(__AVX__)
#define JOB_CTX_X86_OFF_YMM 624
#endif

_Static_assert(offsetof(job_context_t, rbx) == JOB_CTX_X86_OFF_RBX, "job_context_t rbx offset");
_Static_assert(offsetof(job_context_t, rbp) == JOB_CTX_X86_OFF_RBP, "job_context_t rbp offset");
_Static_assert(offsetof(job_context_t, r12) == JOB_CTX_X86_OFF_R12, "job_context_t r12 offset");
_Static_assert(offsetof(job_context_t, r13) == JOB_CTX_X86_OFF_R13, "job_context_t r13 offset");
_Static_assert(offsetof(job_context_t, r14) == JOB_CTX_X86_OFF_R14, "job_context_t r14 offset");
_Static_assert(offsetof(job_context_t, r15) == JOB_CTX_X86_OFF_R15, "job_context_t r15 offset");
_Static_assert(offsetof(job_context_t, sp) == JOB_CTX_X86_OFF_SP, "job_context_t sp offset");
_Static_assert(offsetof(job_context_t, rip) == JOB_CTX_X86_OFF_RIP, "job_context_t rip offset");
_Static_assert(offsetof(job_context_t, entry) == JOB_CTX_X86_OFF_ENTRY, "job_context_t entry offset");
_Static_assert(offsetof(job_context_t, arg0) == JOB_CTX_X86_OFF_ARG0, "job_context_t arg0 offset");
_Static_assert(offsetof(job_context_t, stack_canary) == JOB_CTX_X86_OFF_CANARY, "job_context_t stack_canary offset");
_Static_assert(offsetof(job_context_t, fxsave_area) == JOB_CTX_X86_OFF_FXSAVE, "job_context_t fxsave_area offset");
_Static_assert((JOB_CTX_X86_OFF_FXSAVE % 16) == 0, "fxsave_area offset must be 16-byte aligned");

#if defined(__AVX512F__)
_Static_assert(offsetof(job_context_t, k_mask) == JOB_CTX_X86_OFF_KMASK, "job_context_t k_mask offset");
_Static_assert(offsetof(job_context_t, zmm) == JOB_CTX_X86_OFF_ZMM, "job_context_t zmm offset");
_Static_assert((JOB_CTX_X86_OFF_ZMM % 64) == 0, "zmm offset must be 64-byte aligned");
_Static_assert(sizeof(job_context_t) == 2768u, "job_context_t size must match asm layout (avx512)");
#elif defined(__AVX__)
_Static_assert(offsetof(job_context_t, ymm) == JOB_CTX_X86_OFF_YMM, "job_context_t ymm offset");
_Static_assert((JOB_CTX_X86_OFF_YMM % 32) == 0, "ymm offset must be 32-byte aligned");
_Static_assert(sizeof(job_context_t) == 1136u, "job_context_t size must match asm layout (avx)");
#else
_Static_assert(sizeof(job_context_t) == 608u, "job_context_t size must match asm layout (sse)");
#endif

extern void job_context_x86_bootstrap(void);

void job_context_init(job_context_t *ctx, void (*entry)(uintptr_t), uintptr_t arg0, void *stack, size_t stack_size) {
    if (!ctx || !entry || !stack || stack_size == 0u) {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));

    /*
     * Seed FP/MMX/XMM control state (incl. MXCSR) for a fresh context.
     * Restoring an all-zero FXSAVE image can leave exceptions unmasked and
     * cause SIGFPE in floating-point heavy code.
     */
    __asm__ volatile("fxsave64 %0" : "=m"(ctx->fxsave_area));

    /* Capture the creating thread's stack-protector canary so that the
     * fiber starts with a valid canary even before its first swap. */
    __asm__ volatile("movq %%fs:40, %0" : "=r"(ctx->stack_canary));

    uintptr_t sp = (uintptr_t)stack + stack_size;
    sp &= ~(uintptr_t)0xFu; /* SysV x86-64: 16-byte alignment */

    /* Enter via bootstrap (jmp, not call). Bootstrap will call entry(arg0). */
    ctx->sp = (uint64_t)sp;
    ctx->rip = (uint64_t)(uintptr_t)&job_context_x86_bootstrap;
    ctx->entry = (uint64_t)(uintptr_t)entry;
    ctx->arg0 = (uint64_t)arg0;

    /* Arrange for bootstrap to have a stable pointer to its owning context via a callee-saved reg. */
    ctx->r12 = (uint64_t)(uintptr_t)ctx;
}

__asm__(
    ".text\n"
    ".p2align 4\n"
    ".global job_context_x86_bootstrap\n"
    ".type job_context_x86_bootstrap,%function\n"
    "job_context_x86_bootstrap:\n"
    /* r12 holds job_context_t* (set in job_context_init and preserved by swap). */
    /* Restore the stack-protector canary for this fiber before calling entry. */
    "movq " XSTR(JOB_CTX_X86_OFF_CANARY) "(%r12), %rax\n"
    "movq %rax, %fs:40\n"
    "movq " XSTR(JOB_CTX_X86_OFF_ARG0) "(%r12), %rdi\n" /* arg0 */
    "movq " XSTR(JOB_CTX_X86_OFF_ENTRY) "(%r12), %rax\n" /* entry */
    "call *%rax\n"
    /* entry is expected not to return; if it does, crash deterministically. */
    "ud2\n"
    ".size job_context_x86_bootstrap, .-job_context_x86_bootstrap\n"
    ".p2align 4\n"
    ".global job_context_swap\n"
    ".type job_context_swap,%function\n"
    "job_context_swap:\n"
    /* rdi=from, rsi=to */
    "movq %rbx, " XSTR(JOB_CTX_X86_OFF_RBX) "(%rdi)\n"
    "movq %rbp, " XSTR(JOB_CTX_X86_OFF_RBP) "(%rdi)\n"
    "movq %r12, " XSTR(JOB_CTX_X86_OFF_R12) "(%rdi)\n"
    "movq %r13, " XSTR(JOB_CTX_X86_OFF_R13) "(%rdi)\n"
    "movq %r14, " XSTR(JOB_CTX_X86_OFF_R14) "(%rdi)\n"
    "movq %r15, " XSTR(JOB_CTX_X86_OFF_R15) "(%rdi)\n"
    "movq %rsp, " XSTR(JOB_CTX_X86_OFF_SP) "(%rdi)\n"
    "leaq 0f(%rip), %rax\n"
    "movq %rax, " XSTR(JOB_CTX_X86_OFF_RIP) "(%rdi)\n"

    /* Save GCC stack-protector canary (%fs:40 == %fs:0x28). */
    "movq %fs:40, %rax\n"
    "movq %rax, " XSTR(JOB_CTX_X86_OFF_CANARY) "(%rdi)\n"

    /* Save FP/MMX/XMM state. */
    "fxsave64 " XSTR(JOB_CTX_X86_OFF_FXSAVE) "(%rdi)\n"

#if defined(__AVX512F__)
    /* Save opmask k0-k7. */
    "kmovq %k0,  " XSTR(JOB_CTX_X86_OFF_KMASK) "+0(%rdi)\n"
    "kmovq %k1,  " XSTR(JOB_CTX_X86_OFF_KMASK) "+8(%rdi)\n"
    "kmovq %k2,  " XSTR(JOB_CTX_X86_OFF_KMASK) "+16(%rdi)\n"
    "kmovq %k3,  " XSTR(JOB_CTX_X86_OFF_KMASK) "+24(%rdi)\n"
    "kmovq %k4,  " XSTR(JOB_CTX_X86_OFF_KMASK) "+32(%rdi)\n"
    "kmovq %k5,  " XSTR(JOB_CTX_X86_OFF_KMASK) "+40(%rdi)\n"
    "kmovq %k6,  " XSTR(JOB_CTX_X86_OFF_KMASK) "+48(%rdi)\n"
    "kmovq %k7,  " XSTR(JOB_CTX_X86_OFF_KMASK) "+56(%rdi)\n"

    /* Save ZMM0-31. */
    "vmovdqu64 %zmm0,  " XSTR(JOB_CTX_X86_OFF_ZMM) "+(0*64)(%rdi)\n"
    "vmovdqu64 %zmm1,  " XSTR(JOB_CTX_X86_OFF_ZMM) "+(1*64)(%rdi)\n"
    "vmovdqu64 %zmm2,  " XSTR(JOB_CTX_X86_OFF_ZMM) "+(2*64)(%rdi)\n"
    "vmovdqu64 %zmm3,  " XSTR(JOB_CTX_X86_OFF_ZMM) "+(3*64)(%rdi)\n"
    "vmovdqu64 %zmm4,  " XSTR(JOB_CTX_X86_OFF_ZMM) "+(4*64)(%rdi)\n"
    "vmovdqu64 %zmm5,  " XSTR(JOB_CTX_X86_OFF_ZMM) "+(5*64)(%rdi)\n"
    "vmovdqu64 %zmm6,  " XSTR(JOB_CTX_X86_OFF_ZMM) "+(6*64)(%rdi)\n"
    "vmovdqu64 %zmm7,  " XSTR(JOB_CTX_X86_OFF_ZMM) "+(7*64)(%rdi)\n"
    "vmovdqu64 %zmm8,  " XSTR(JOB_CTX_X86_OFF_ZMM) "+(8*64)(%rdi)\n"
    "vmovdqu64 %zmm9,  " XSTR(JOB_CTX_X86_OFF_ZMM) "+(9*64)(%rdi)\n"
    "vmovdqu64 %zmm10, " XSTR(JOB_CTX_X86_OFF_ZMM) "+(10*64)(%rdi)\n"
    "vmovdqu64 %zmm11, " XSTR(JOB_CTX_X86_OFF_ZMM) "+(11*64)(%rdi)\n"
    "vmovdqu64 %zmm12, " XSTR(JOB_CTX_X86_OFF_ZMM) "+(12*64)(%rdi)\n"
    "vmovdqu64 %zmm13, " XSTR(JOB_CTX_X86_OFF_ZMM) "+(13*64)(%rdi)\n"
    "vmovdqu64 %zmm14, " XSTR(JOB_CTX_X86_OFF_ZMM) "+(14*64)(%rdi)\n"
    "vmovdqu64 %zmm15, " XSTR(JOB_CTX_X86_OFF_ZMM) "+(15*64)(%rdi)\n"
    "vmovdqu64 %zmm16, " XSTR(JOB_CTX_X86_OFF_ZMM) "+(16*64)(%rdi)\n"
    "vmovdqu64 %zmm17, " XSTR(JOB_CTX_X86_OFF_ZMM) "+(17*64)(%rdi)\n"
    "vmovdqu64 %zmm18, " XSTR(JOB_CTX_X86_OFF_ZMM) "+(18*64)(%rdi)\n"
    "vmovdqu64 %zmm19, " XSTR(JOB_CTX_X86_OFF_ZMM) "+(19*64)(%rdi)\n"
    "vmovdqu64 %zmm20, " XSTR(JOB_CTX_X86_OFF_ZMM) "+(20*64)(%rdi)\n"
    "vmovdqu64 %zmm21, " XSTR(JOB_CTX_X86_OFF_ZMM) "+(21*64)(%rdi)\n"
    "vmovdqu64 %zmm22, " XSTR(JOB_CTX_X86_OFF_ZMM) "+(22*64)(%rdi)\n"
    "vmovdqu64 %zmm23, " XSTR(JOB_CTX_X86_OFF_ZMM) "+(23*64)(%rdi)\n"
    "vmovdqu64 %zmm24, " XSTR(JOB_CTX_X86_OFF_ZMM) "+(24*64)(%rdi)\n"
    "vmovdqu64 %zmm25, " XSTR(JOB_CTX_X86_OFF_ZMM) "+(25*64)(%rdi)\n"
    "vmovdqu64 %zmm26, " XSTR(JOB_CTX_X86_OFF_ZMM) "+(26*64)(%rdi)\n"
    "vmovdqu64 %zmm27, " XSTR(JOB_CTX_X86_OFF_ZMM) "+(27*64)(%rdi)\n"
    "vmovdqu64 %zmm28, " XSTR(JOB_CTX_X86_OFF_ZMM) "+(28*64)(%rdi)\n"
    "vmovdqu64 %zmm29, " XSTR(JOB_CTX_X86_OFF_ZMM) "+(29*64)(%rdi)\n"
    "vmovdqu64 %zmm30, " XSTR(JOB_CTX_X86_OFF_ZMM) "+(30*64)(%rdi)\n"
    "vmovdqu64 %zmm31, " XSTR(JOB_CTX_X86_OFF_ZMM) "+(31*64)(%rdi)\n"

#elif defined(__AVX__)
    /* Save YMM0-15. */
    "vmovdqu %ymm0,  " XSTR(JOB_CTX_X86_OFF_YMM) "+(0*32)(%rdi)\n"
    "vmovdqu %ymm1,  " XSTR(JOB_CTX_X86_OFF_YMM) "+(1*32)(%rdi)\n"
    "vmovdqu %ymm2,  " XSTR(JOB_CTX_X86_OFF_YMM) "+(2*32)(%rdi)\n"
    "vmovdqu %ymm3,  " XSTR(JOB_CTX_X86_OFF_YMM) "+(3*32)(%rdi)\n"
    "vmovdqu %ymm4,  " XSTR(JOB_CTX_X86_OFF_YMM) "+(4*32)(%rdi)\n"
    "vmovdqu %ymm5,  " XSTR(JOB_CTX_X86_OFF_YMM) "+(5*32)(%rdi)\n"
    "vmovdqu %ymm6,  " XSTR(JOB_CTX_X86_OFF_YMM) "+(6*32)(%rdi)\n"
    "vmovdqu %ymm7,  " XSTR(JOB_CTX_X86_OFF_YMM) "+(7*32)(%rdi)\n"
    "vmovdqu %ymm8,  " XSTR(JOB_CTX_X86_OFF_YMM) "+(8*32)(%rdi)\n"
    "vmovdqu %ymm9,  " XSTR(JOB_CTX_X86_OFF_YMM) "+(9*32)(%rdi)\n"
    "vmovdqu %ymm10, " XSTR(JOB_CTX_X86_OFF_YMM) "+(10*32)(%rdi)\n"
    "vmovdqu %ymm11, " XSTR(JOB_CTX_X86_OFF_YMM) "+(11*32)(%rdi)\n"
    "vmovdqu %ymm12, " XSTR(JOB_CTX_X86_OFF_YMM) "+(12*32)(%rdi)\n"
    "vmovdqu %ymm13, " XSTR(JOB_CTX_X86_OFF_YMM) "+(13*32)(%rdi)\n"
    "vmovdqu %ymm14, " XSTR(JOB_CTX_X86_OFF_YMM) "+(14*32)(%rdi)\n"
    "vmovdqu %ymm15, " XSTR(JOB_CTX_X86_OFF_YMM) "+(15*32)(%rdi)\n"
#endif

    /* Restore GPRs from *to. */
    "movq " XSTR(JOB_CTX_X86_OFF_RBX) "(%rsi), %rbx\n"
    "movq " XSTR(JOB_CTX_X86_OFF_RBP) "(%rsi), %rbp\n"
    "movq " XSTR(JOB_CTX_X86_OFF_R12) "(%rsi), %r12\n"
    "movq " XSTR(JOB_CTX_X86_OFF_R13) "(%rsi), %r13\n"
    "movq " XSTR(JOB_CTX_X86_OFF_R14) "(%rsi), %r14\n"
    "movq " XSTR(JOB_CTX_X86_OFF_R15) "(%rsi), %r15\n"
    "movq " XSTR(JOB_CTX_X86_OFF_SP) "(%rsi), %rsp\n"

    /* Restore FP/MMX/XMM state. */
    "fxrstor64 " XSTR(JOB_CTX_X86_OFF_FXSAVE) "(%rsi)\n"

#if defined(__AVX512F__)
    /* Restore opmask k0-k7. */
    "kmovq " XSTR(JOB_CTX_X86_OFF_KMASK) "+0(%rsi), %k0\n"
    "kmovq " XSTR(JOB_CTX_X86_OFF_KMASK) "+8(%rsi), %k1\n"
    "kmovq " XSTR(JOB_CTX_X86_OFF_KMASK) "+16(%rsi), %k2\n"
    "kmovq " XSTR(JOB_CTX_X86_OFF_KMASK) "+24(%rsi), %k3\n"
    "kmovq " XSTR(JOB_CTX_X86_OFF_KMASK) "+32(%rsi), %k4\n"
    "kmovq " XSTR(JOB_CTX_X86_OFF_KMASK) "+40(%rsi), %k5\n"
    "kmovq " XSTR(JOB_CTX_X86_OFF_KMASK) "+48(%rsi), %k6\n"
    "kmovq " XSTR(JOB_CTX_X86_OFF_KMASK) "+56(%rsi), %k7\n"

    /* Restore ZMM0-31. */
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(0*64)(%rsi),  %zmm0\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(1*64)(%rsi),  %zmm1\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(2*64)(%rsi),  %zmm2\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(3*64)(%rsi),  %zmm3\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(4*64)(%rsi),  %zmm4\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(5*64)(%rsi),  %zmm5\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(6*64)(%rsi),  %zmm6\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(7*64)(%rsi),  %zmm7\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(8*64)(%rsi),  %zmm8\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(9*64)(%rsi),  %zmm9\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(10*64)(%rsi), %zmm10\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(11*64)(%rsi), %zmm11\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(12*64)(%rsi), %zmm12\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(13*64)(%rsi), %zmm13\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(14*64)(%rsi), %zmm14\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(15*64)(%rsi), %zmm15\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(16*64)(%rsi), %zmm16\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(17*64)(%rsi), %zmm17\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(18*64)(%rsi), %zmm18\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(19*64)(%rsi), %zmm19\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(20*64)(%rsi), %zmm20\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(21*64)(%rsi), %zmm21\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(22*64)(%rsi), %zmm22\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(23*64)(%rsi), %zmm23\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(24*64)(%rsi), %zmm24\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(25*64)(%rsi), %zmm25\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(26*64)(%rsi), %zmm26\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(27*64)(%rsi), %zmm27\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(28*64)(%rsi), %zmm28\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(29*64)(%rsi), %zmm29\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(30*64)(%rsi), %zmm30\n"
    "vmovdqu64 " XSTR(JOB_CTX_X86_OFF_ZMM) "+(31*64)(%rsi), %zmm31\n"

#elif defined(__AVX__)
    /* Restore YMM0-15. */
    "vmovdqu " XSTR(JOB_CTX_X86_OFF_YMM) "+(0*32)(%rsi),  %ymm0\n"
    "vmovdqu " XSTR(JOB_CTX_X86_OFF_YMM) "+(1*32)(%rsi),  %ymm1\n"
    "vmovdqu " XSTR(JOB_CTX_X86_OFF_YMM) "+(2*32)(%rsi),  %ymm2\n"
    "vmovdqu " XSTR(JOB_CTX_X86_OFF_YMM) "+(3*32)(%rsi),  %ymm3\n"
    "vmovdqu " XSTR(JOB_CTX_X86_OFF_YMM) "+(4*32)(%rsi),  %ymm4\n"
    "vmovdqu " XSTR(JOB_CTX_X86_OFF_YMM) "+(5*32)(%rsi),  %ymm5\n"
    "vmovdqu " XSTR(JOB_CTX_X86_OFF_YMM) "+(6*32)(%rsi),  %ymm6\n"
    "vmovdqu " XSTR(JOB_CTX_X86_OFF_YMM) "+(7*32)(%rsi),  %ymm7\n"
    "vmovdqu " XSTR(JOB_CTX_X86_OFF_YMM) "+(8*32)(%rsi),  %ymm8\n"
    "vmovdqu " XSTR(JOB_CTX_X86_OFF_YMM) "+(9*32)(%rsi),  %ymm9\n"
    "vmovdqu " XSTR(JOB_CTX_X86_OFF_YMM) "+(10*32)(%rsi), %ymm10\n"
    "vmovdqu " XSTR(JOB_CTX_X86_OFF_YMM) "+(11*32)(%rsi), %ymm11\n"
    "vmovdqu " XSTR(JOB_CTX_X86_OFF_YMM) "+(12*32)(%rsi), %ymm12\n"
    "vmovdqu " XSTR(JOB_CTX_X86_OFF_YMM) "+(13*32)(%rsi), %ymm13\n"
    "vmovdqu " XSTR(JOB_CTX_X86_OFF_YMM) "+(14*32)(%rsi), %ymm14\n"
    "vmovdqu " XSTR(JOB_CTX_X86_OFF_YMM) "+(15*32)(%rsi), %ymm15\n"
#endif

    /* Restore GCC stack-protector canary into %fs:40 for the target fiber. */
    "movq " XSTR(JOB_CTX_X86_OFF_CANARY) "(%rsi), %rax\n"
    "movq %rax, %fs:40\n"

    "movq " XSTR(JOB_CTX_X86_OFF_RIP) "(%rsi), %rax\n"
    "jmp *%rax\n"
    "0:\n"
    "ret\n"
    ".size job_context_swap, .-job_context_swap\n");

#else

void job_context_swap(job_context_t *from, job_context_t *to) {

#ifdef TRACY_ENABLE
    // We are currently executing on `from`'s stack.
    TracyFiberLeave();
#endif
    (void)swapcontext(from, to);
#ifdef TRACY_ENABLE
    // We have switched back to `from`'s stack.
    TracyFiberEnter(from->tracy_name);
#endif
}

#endif

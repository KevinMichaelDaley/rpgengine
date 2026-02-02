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

#else

void job_context_swap(job_context_t *from, job_context_t *to) {
    (void)swapcontext(from, to);
}

#endif

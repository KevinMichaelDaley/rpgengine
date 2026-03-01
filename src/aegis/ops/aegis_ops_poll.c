/**
 * @file aegis_ops_poll.c
 * @brief Async poll and wait instruction handlers.
 *
 * poll: non-blocking status read. Copies result on COMPLETE.
 * wait: polls, returns false if PENDING (interpreter should wait-yield).
 */

#include "ferrum/aegis/aegis_ops_async.h"
#include "ferrum/aegis/aegis_vm.h"
#include "ferrum/aegis/aegis_async.h"
#include "ferrum/aegis/aegis_decode.h"

#include <string.h>
#include <stdatomic.h>

/**
 * @brief Find the async task tracking entry by handle (heap offset).
 *
 * @return Index into vm->async_tasks[], or -1 if not found.
 */
static int32_t find_task_by_handle(const aegis_vm_t *vm, int32_t handle) {
    void *expected_ptr = vm->memory.base + handle;
    for (uint32_t i = 0; i < vm->async_task_count; i++) {
        if (vm->async_tasks[i].result_ptr == expected_ptr) {
            return (int32_t)i;
        }
    }
    return -1;
}

bool aegis_op_poll(aegis_vm_t *vm, const aegis_decode_result_t *d) {
    int32_t handle = vm->regs[d->raw_c].i32;
    int32_t idx = find_task_by_handle(vm, handle);
    if (idx < 0) {
        return false; /* Invalid handle. */
    }

    uint32_t status = atomic_load_explicit(
        &vm->async_tasks[idx].status, memory_order_acquire);

    vm->regs[d->raw_b].u32 = status;

    if (status == AEGIS_ASYNC_COMPLETE) {
        /* Copy 16 bytes from result slot into result register. */
        memcpy(&vm->regs[d->raw_a], vm->async_tasks[idx].result_ptr, 16);
    }

    return true;
}

bool aegis_op_wait(aegis_vm_t *vm, const aegis_decode_result_t *d) {
    int32_t handle = vm->regs[d->raw_c].i32;
    int32_t idx = find_task_by_handle(vm, handle);
    if (idx < 0) {
        return false; /* Invalid handle — treat as error. */
    }

    uint32_t status = atomic_load_explicit(
        &vm->async_tasks[idx].status, memory_order_acquire);

    vm->regs[d->raw_b].u32 = status;

    if (status == AEGIS_ASYNC_PENDING) {
        /* Task not done yet — caller should wait-yield. */
        return false;
    }

    /* COMPLETE or ERROR: copy result and advance normally. */
    if (status == AEGIS_ASYNC_COMPLETE) {
        memcpy(&vm->regs[d->raw_a], vm->async_tasks[idx].result_ptr, 16);
    }
    return true;
}

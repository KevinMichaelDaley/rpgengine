/**
 * @file aegis_ops_async.c
 * @brief Async submission handlers: vis_test and nav_query.
 *
 * Both follow the same pattern:
 * 1. Check async task limit (config.max_async_tasks).
 * 2. Allocate a 16-byte result slot in the heap arena.
 * 3. Build an aegis_async_task_t with params and result_ptr.
 * 4. Submit to the async buffer (MPSC ring).
 * 5. Track the task in vm->async_tasks[] for poll/wait.
 * 6. Store the handle (heap offset) in the destination register.
 */

#include "ferrum/aegis/aegis_ops_async.h"
#include "ferrum/aegis/aegis_vm.h"
#include "ferrum/aegis/aegis_async.h"
#include "ferrum/aegis/aegis_decode.h"

#include <string.h>

/** @brief Result slot size in the heap arena (16 bytes = one register). */
#define ASYNC_RESULT_SLOT_SIZE 16

/**
 * @brief Common async submission logic for both vis_test and nav_query.
 *
 * @param vm        VM instance.
 * @param d         Decoded instruction (a=handle, b=param1, c=param2).
 * @param task_type AEGIS_TASK_VIS_TEST or AEGIS_TASK_NAV_QUERY.
 * @return true on success, false on limit/buffer/heap error.
 */
static bool submit_async(aegis_vm_t *vm, const aegis_decode_result_t *d,
                         uint32_t task_type) {
    /* Check per-VM async task limit. */
    if (vm->async_task_count >= vm->config.max_async_tasks) {
        return false;
    }
    if (vm->async_task_count >= 32) {
        return false; /* Hard cap on tracking array. */
    }

    /* Allocate result slot in heap arena. */
    int32_t offset = aegis_memory_alloc(&vm->memory, ASYNC_RESULT_SLOT_SIZE);
    if (offset < 0) {
        return false;
    }

    /* Build task. */
    aegis_async_task_t task;
    memset(&task, 0, sizeof(task));
    atomic_store(&task.status, AEGIS_ASYNC_PENDING);
    task.task_type  = task_type;
    task.result_ptr = vm->memory.base + offset;
    task.result_cap = ASYNC_RESULT_SLOT_SIZE;

    /* Copy vec3 params (2 × 12 bytes = 24 bytes into params[64]). */
    memcpy(task.params,      vm->regs[d->raw_b].vec3, 12);
    memcpy(task.params + 12, vm->regs[d->raw_c].vec3, 12);

    /* Track in VM's local task array FIRST so we can give the buffer
     * copy a pointer to the VM-side status for executor writeback. */
    uint32_t idx = vm->async_task_count;
    vm->async_tasks[idx] = task;
    vm->async_tasks[idx].result_ptr = vm->memory.base + offset;
    vm->async_task_count++;

    /* Set status_ptr on the task to point at the VM's tracking entry.
     * The executor will atomically write COMPLETE/ERROR here after
     * writing results to result_ptr. */
    task.status_ptr = &vm->async_tasks[idx].status;

    /* Submit to async buffer (copies task including status_ptr). */
    if (!aegis_async_buffer_submit(vm->async_buffer, &task)) {
        vm->async_task_count--; /* Roll back tracking. */
        return false;
    }

    /* Store handle = heap offset in destination register. */
    vm->regs[d->raw_a].i32 = offset;
    return true;
}

bool aegis_op_vis_test(aegis_vm_t *vm, const aegis_decode_result_t *d) {
    return submit_async(vm, d, AEGIS_TASK_VIS_TEST);
}

bool aegis_op_nav_query(aegis_vm_t *vm, const aegis_decode_result_t *d) {
    return submit_async(vm, d, AEGIS_TASK_NAV_QUERY);
}

/**
 * @file aegis_yield.c
 * @brief Aegis yield, force-yield, wait-yield, and exit.
 *
 * Per ref/aegis_bytecode_spec.md §6.1.
 */

#include "ferrum/aegis/aegis_vm.h"

aegis_vm_status_t aegis_vm_yield(aegis_vm_t *vm) {
    /* Explicit yield requires call depth 0. */
    if (aegis_memory_call_depth(&vm->memory) != 0) {
        vm->status = AEGIS_VM_ERROR;
        vm->exit_code = 1; /* yield-at-depth error */
        return AEGIS_VM_ERROR;
    }

    /* Reset heap arena (explicit yield only). */
    aegis_memory_heap_reset(&vm->memory);

    /* Reset fuel. */
    vm->fuel = vm->config.fuel_budget;

    vm->status = AEGIS_VM_YIELDED;
    return AEGIS_VM_YIELDED;
}

void aegis_vm_force_yield(aegis_vm_t *vm) {
    /* Force-yield: no heap reset, no stack change. Just reset fuel. */
    vm->fuel = vm->config.fuel_budget;
    vm->status = AEGIS_VM_FORCE_YIELDED;
}

void aegis_vm_wait_yield(aegis_vm_t *vm) {
    /* Wait-yield: no heap reset, no stack change, no fuel reset. */
    vm->status = AEGIS_VM_WAIT_YIELDED;
}

void aegis_vm_exit(aegis_vm_t *vm, uint32_t exit_code) {
    vm->status    = AEGIS_VM_EXITED;
    vm->exit_code = exit_code;
    vm->alive     = false;
}

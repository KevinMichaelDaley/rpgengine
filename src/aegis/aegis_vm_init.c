/**
 * @file aegis_vm_init.c
 * @brief Aegis VM initialization and fuel management.
 *
 * Per ref/aegis_bytecode_spec.md §6.
 */

#include "ferrum/aegis/aegis_vm.h"
#include <string.h>

bool aegis_vm_init(aegis_vm_t *vm, const aegis_bytecode_t *bytecode,
                   const aegis_config_t *config,
                   uint8_t *arena_buf, uint32_t arena_size) {
    memset(vm->regs, 0, sizeof(vm->regs));
    vm->pc        = 0;
    vm->bytecode  = bytecode;
    vm->config    = *config;
    vm->status    = AEGIS_VM_YIELDED;
    vm->exit_code = 0;
    vm->alive     = true;

    /* Initialize memory layout using bytecode's static_size. */
    if (!aegis_memory_init(&vm->memory, arena_buf, arena_size,
                           bytecode->static_size, config->stack_max)) {
        return false;
    }

    /* Set initial fuel. */
    vm->fuel = config->fuel_budget;
    return true;
}

void aegis_vm_reset_fuel(aegis_vm_t *vm) {
    vm->fuel = vm->config.fuel_budget;
}

bool aegis_vm_consume_fuel(aegis_vm_t *vm) {
    if (vm->fuel == 0) {
        return false;
    }
    vm->fuel--;
    return true;
}

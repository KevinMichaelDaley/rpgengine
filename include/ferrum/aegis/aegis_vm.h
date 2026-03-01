/**
 * @file aegis_vm.h
 * @brief Aegis VM state: registers, PC, fuel, memory, execution status.
 *
 * Per ref/aegis_bytecode_spec.md §3, §6.
 *
 * The VM is a coroutine: it resumes with an event, executes until yield/exit/fuel
 * exhaustion, then returns control to the engine with an update set.
 *
 * Ownership: the VM owns its register file and references the memory and bytecode.
 *   The caller owns the backing arena buffer and the bytecode module.
 * Nullability: all init parameters must be non-NULL.
 */

#ifndef FERRUM_AEGIS_VM_H
#define FERRUM_AEGIS_VM_H

#include <stdbool.h>
#include <stdint.h>
#include "ferrum/aegis/aegis_bytecode.h"
#include "ferrum/aegis/aegis_config.h"
#include "ferrum/aegis/aegis_event.h"
#include "ferrum/aegis/aegis_memory.h"
#include "ferrum/aegis/aegis_types.h"
#include "ferrum/aegis/aegis_update.h"

#ifdef __cplusplus
extern "C" {
#endif

struct script_entity_view;

/**
 * @brief VM execution status after a run.
 */
typedef enum aegis_vm_status {
    AEGIS_VM_YIELDED      = 0,  /**< Script explicitly yielded. */
    AEGIS_VM_FORCE_YIELDED = 1, /**< Fuel exhausted; force-yielded. */
    AEGIS_VM_WAIT_YIELDED  = 2, /**< Yielded via wait instruction. */
    AEGIS_VM_EXITED        = 3, /**< Script called exit (permanent). */
    AEGIS_VM_ERROR         = 4, /**< Runtime error (OOB, stack overflow, etc). */
} aegis_vm_status_t;

/**
 * @brief Aegis VM instance state.
 *
 * Contains all mutable state for one script execution.
 * The register file, PC, fuel, and memory manager live here.
 */
typedef struct aegis_vm {
    /** Register file: 256 × 128-bit registers. */
    aegis_register_t regs[AEGIS_REGISTER_COUNT];

    /** Program counter (instruction index). */
    uint32_t pc;

    /** Remaining fuel (instructions left before force-yield). */
    uint32_t fuel;

    /** Three-zone memory manager. */
    aegis_memory_t memory;

    /** Reference to compiled bytecode (not owned). */
    const aegis_bytecode_t *bytecode;

    /** VM configuration (fuel budget, limits). */
    aegis_config_t config;

    /** Current event being processed (set before resume, NULL if none). */
    const aegis_event_t *event;

    /** Entity snapshot view for query instructions (set by runtime). */
    const struct script_entity_view *entity_view;

    /** Update set for accumulating state mutations (set by runtime). */
    aegis_update_set_t *update_set;

    /** Staging area for the current update being built. */
    aegis_state_update_t staging;

    /** Current execution status. */
    aegis_vm_status_t status;

    /** Exit code (valid when status == AEGIS_VM_EXITED or AEGIS_VM_ERROR). */
    uint32_t exit_code;

    /** Whether the VM has been initialized and not yet exited. */
    bool alive;
} aegis_vm_t;

/**
 * @brief Initialize a VM instance.
 *
 * Sets up the register file, memory layout, and fuel from config.
 * The VM starts in a resumable state at PC=0.
 *
 * @param vm         VM instance to initialize.
 * @param bytecode   Compiled bytecode module (caller-owned, must outlive VM).
 * @param config     VM configuration.
 * @param arena_buf  Backing memory buffer (caller-owned, must outlive VM).
 * @param arena_size Size of arena_buf in bytes.
 * @return true on success, false on invalid config or memory init failure.
 *
 * Side effects: zeroes registers, initializes memory zones.
 */
bool aegis_vm_init(aegis_vm_t *vm, const aegis_bytecode_t *bytecode,
                   const aegis_config_t *config,
                   uint8_t *arena_buf, uint32_t arena_size);

/**
 * @brief Reset the VM for a new resume cycle.
 *
 * Called before each resume. Resets fuel to budget.
 * Does NOT reset registers, PC, or memory (those are continuation state).
 *
 * @param vm VM instance.
 */
void aegis_vm_reset_fuel(aegis_vm_t *vm);

/**
 * @brief Consume one unit of fuel.
 *
 * @param vm VM instance.
 * @return true if fuel remains, false if exhausted (force-yield needed).
 */
bool aegis_vm_consume_fuel(aegis_vm_t *vm);

/**
 * @brief Execute an explicit yield.
 *
 * Resets the heap arena and fuel. Validates that call depth is 0.
 *
 * @param vm VM instance.
 * @return AEGIS_VM_YIELDED on success, AEGIS_VM_ERROR if call depth > 0.
 */
aegis_vm_status_t aegis_vm_yield(aegis_vm_t *vm);

/**
 * @brief Execute a force-yield (fuel exhaustion).
 *
 * Does NOT reset heap or stack. Resets fuel only.
 * The script will resume from the current PC.
 *
 * @param vm VM instance.
 */
void aegis_vm_force_yield(aegis_vm_t *vm);

/**
 * @brief Execute a wait-yield.
 *
 * Does NOT reset heap, stack, or fuel. The script will
 * re-execute the wait instruction on next resume.
 *
 * @param vm VM instance.
 */
void aegis_vm_wait_yield(aegis_vm_t *vm);

/**
 * @brief Execute an exit instruction.
 *
 * Permanently terminates the script.
 *
 * @param vm        VM instance.
 * @param exit_code Error/status code.
 */
void aegis_vm_exit(aegis_vm_t *vm, uint32_t exit_code);

/**
 * @brief Run the VM until yield, exit, error, or fuel exhaustion.
 *
 * Main interpreter loop. Fetches, decodes, and dispatches instructions.
 * Returns the reason execution stopped.
 *
 * @param vm VM instance (must be initialized and alive).
 * @return Execution status indicating why the loop terminated.
 *
 * Side effects: modifies registers, memory, PC, fuel.
 */
aegis_vm_status_t aegis_vm_run(aegis_vm_t *vm);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_AEGIS_VM_H */

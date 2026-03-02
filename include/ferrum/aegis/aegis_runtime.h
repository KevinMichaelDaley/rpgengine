/**
 * @file aegis_runtime.h
 * @brief Aegis script runtime: manages script instances, event routing,
 *        and fiber-based execution.
 *
 * Each loaded script gets its own aegis_vm_t, event queue, and arena.
 * Scripts are launched as long-lived fibers via the engine's job system.
 * Force-yield (fuel exhaustion) maps to job_yield() for natural preemption.
 *
 * Types exposed:
 *   - aegis_script_instance_t  — per-script state (VM + event queue)
 *   - aegis_script_runtime_t   — runtime manager (instance table + topics)
 */

#ifndef FERRUM_AEGIS_RUNTIME_H
#define FERRUM_AEGIS_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>
#include "ferrum/aegis/aegis_bytecode.h"
#include "ferrum/aegis/aegis_config.h"
#include "ferrum/aegis/aegis_event.h"
#include "ferrum/aegis/aegis_vm.h"

#ifdef __cplusplus
extern "C" {
#endif

struct job_system;

/** Sentinel for invalid script IDs. */
#define AEGIS_SCRIPT_ID_INVALID ((uint32_t)0xFFFFFFFF)

/** Maximum number of scripts in the registry. */
#define AEGIS_REGISTRY_MAX 128

/**
 * @brief A registered script entry (compiled bytecode, not yet spawned).
 *
 * Scripts are registered via `script load` and lazily spawned when their
 * subscribed topic fires for the first time.
 */
typedef struct aegis_script_entry {
    /** Compiled bytecode (owned copy of instructions). */
    aegis_bytecode_t bytecode;

    /** Debug name. */
    char name[64];

    /** Whether this registry slot is occupied. */
    bool registered;

    /** Whether this script has been spawned (has an active instance). */
    bool spawned;

    /** If spawned, the active instance's script_id. */
    uint32_t instance_id;
} aegis_script_entry_t;

/**
 * @brief Per-script instance state.
 *
 * Each loaded script owns a VM, event queue, and backing arena buffer.
 * The fiber function runs for the lifetime of the script.
 */
typedef struct aegis_script_instance {
    /** VM instance for this script. */
    aegis_vm_t vm;

    /** Per-script event queue (events routed here by topic table). */
    aegis_event_queue_t event_queue;

    /** Backing arena buffer for VM memory (owned). */
    uint8_t *arena_buf;

    /** Bytecode reference (owned copy of instructions). */
    aegis_bytecode_t bytecode;

    /** Script slot index in the runtime's instance array. */
    uint32_t script_id;

    /** Debug name for logging/Tracy. */
    char name[64];

    /** Whether this script is alive and scheduled. */
    bool active;

    /** Whether this script exited and is pending unschedule. */
    bool pending_unschedule;

    /** Ticks since last event while pending unschedule. */
    uint32_t idle_ticks;

    /** Last signal timestamp in microseconds (monotonic). */
    uint64_t last_signal_time_us;

    /** Pointer to owning runtime (for fiber function context). */
    struct aegis_script_runtime *runtime;

    /** Pointer to job system (for fiber function context). */
    struct job_system *job_sys;
} aegis_script_instance_t;

/**
 * @brief Runtime configuration.
 */
typedef struct aegis_runtime_config {
    /** Maximum number of concurrent script instances. */
    uint32_t max_instances;

    /** Maximum total topic subscriptions. */
    uint32_t max_subscriptions;

    /** Per-script event queue capacity. */
    uint32_t event_queue_cap;

    /** VM configuration applied to all script instances. */
    aegis_config_t vm_config;

    /** Minimum microseconds between signals per script. Default: 250. */
    uint32_t signal_rate_limit_us;

    /** Ticks to wait after EXIT before unscheduling. Default: 6. */
    uint32_t idle_grace_ticks;
} aegis_runtime_config_t;

/**
 * @brief Script runtime manager.
 *
 * Owns the instance table, topic routing table, and configuration.
 * Manages script lifecycle: load, start (dispatch fiber), publish
 * events, unload, destroy.
 *
 * Ownership: caller owns the runtime struct. Runtime owns internal
 *   allocations (instance array, topic table).
 * Nullability: all pointer parameters must be non-NULL.
 */
typedef struct aegis_script_runtime {
    /** Fixed-capacity array of script instances (owned). */
    aegis_script_instance_t *instances;

    /** Number of currently active instances. */
    uint32_t instance_count;

    /** Maximum instance capacity. */
    uint32_t instance_cap;

    /** Shared topic routing table. */
    aegis_topic_table_t topics;

    /** Runtime configuration. */
    aegis_runtime_config_t config;

    /** Script registry: compiled bytecodes awaiting lazy spawn. */
    aegis_script_entry_t registry[AEGIS_REGISTRY_MAX];

    /** Number of registered scripts. */
    uint32_t registry_count;

    /** Job system for spawning fibers (set via aegis_script_runtime_set_job_sys). */
    struct job_system *job_sys;

    /** Shared async task buffer for VIS_TEST/NAV_QUERY (set by server, not owned). */
    struct aegis_async_buffer *async_buffer;

    /** Shared entity snapshot view (set by server each tick, not owned). */
    const struct script_entity_view *entity_view;
} aegis_script_runtime_t;

/* ----------------------------------------------------------------------- */
/* Lifecycle API                                                            */
/* ----------------------------------------------------------------------- */

/**
 * @brief Initialize a script runtime.
 *
 * Allocates the instance array and topic table.
 *
 * @param rt   Runtime to initialize. Must not be NULL.
 * @param cfg  Configuration. Must not be NULL.
 * @return true on success, false on allocation failure.
 *
 * Side effects: allocates heap memory for instances and topic table.
 */
bool aegis_script_runtime_init(aegis_script_runtime_t *rt,
                               const aegis_runtime_config_t *cfg);

/**
 * @brief Destroy a script runtime, freeing all resources.
 *
 * Unloads all active scripts and frees internal allocations.
 *
 * @param rt Runtime to destroy. Must not be NULL.
 *
 * Side effects: frees heap memory. Active fibers must have completed
 *   before calling this (caller's responsibility to shut down job system
 *   or wait idle first).
 */
void aegis_script_runtime_destroy(aegis_script_runtime_t *rt);

/* ----------------------------------------------------------------------- */
/* Script management API                                                    */
/* ----------------------------------------------------------------------- */

/**
 * @brief Load a compiled script into the runtime.
 *
 * Allocates a VM instance, arena buffer, and event queue.
 * If the bytecode has a non-zero topic_hash, auto-subscribes.
 * Does NOT start execution — call aegis_script_runtime_start() after.
 *
 * @param rt   Runtime. Must not be NULL, must be initialized.
 * @param name Debug name (copied, truncated to 63 chars).
 * @param bc   Compiled bytecode. Instructions are copied (caller retains
 *             ownership of the original).
 * @return Script ID on success, AEGIS_SCRIPT_ID_INVALID if at capacity
 *         or allocation failure.
 *
 * Side effects: allocates arena buffer and event queue.
 */
uint32_t aegis_script_runtime_load(aegis_script_runtime_t *rt,
                                   const char *name,
                                   const aegis_bytecode_t *bc);

/**
 * @brief Unload a script, freeing its resources and unsubscribing topics.
 *
 * @param rt        Runtime. Must not be NULL.
 * @param script_id Script to unload. Must be a valid, active ID.
 *
 * Side effects: frees arena buffer and event queue. Modifies topic table.
 *   The script's fiber must have exited or been drained before calling.
 */
void aegis_script_runtime_unload(aegis_script_runtime_t *rt,
                                 uint32_t script_id);

/**
 * @brief Start a loaded script's fiber on the job system.
 *
 * Dispatches a long-lived fiber that loops: pop event → run VM →
 * on force-yield, job_yield() → resume. The fiber exits when the
 * script exits or errors.
 *
 * @param rt        Runtime. Must not be NULL.
 * @param script_id Script to start. Must be loaded and active.
 * @param sys       Job system to dispatch the fiber on.
 *
 * Side effects: dispatches a fiber via job_dispatch().
 */
void aegis_script_runtime_start(aegis_script_runtime_t *rt,
                                uint32_t script_id,
                                struct job_system *sys);

/* ----------------------------------------------------------------------- */
/* Event API                                                                */
/* ----------------------------------------------------------------------- */

/**
 * @brief Publish an event to all subscribed scripts.
 *
 * Routes the event through the topic table to each subscribed script's
 * event queue.
 *
 * @param rt Runtime. Must not be NULL.
 * @param ev Event to publish. Must not be NULL.
 *
 * Side effects: pushes event copies into subscriber queues.
 */
void aegis_script_runtime_publish(aegis_script_runtime_t *rt,
                                  const aegis_event_t *ev);

/* ----------------------------------------------------------------------- */
/* Registry API (lazy spawn)                                                */
/* ----------------------------------------------------------------------- */

/**
 * @brief Set the job system used for spawning script fibers.
 *
 * Must be called before any scripts can be lazily spawned.
 *
 * @param rt  Runtime. Must not be NULL.
 * @param sys Job system. Must not be NULL.
 */
void aegis_script_runtime_set_job_sys(aegis_script_runtime_t *rt,
                                      struct job_system *sys);

/**
 * @brief Set the shared async task buffer for all script VMs.
 *
 * Propagates the buffer pointer to all currently active VMs.
 * New scripts loaded after this call also receive the pointer.
 *
 * @param rt  Runtime. Must not be NULL.
 * @param buf Async buffer. May be NULL to disable async ops.
 */
void aegis_script_runtime_set_async_buffer(aegis_script_runtime_t *rt,
                                           struct aegis_async_buffer *buf);

/**
 * @brief Set the shared entity snapshot view for all script VMs.
 *
 * Call this each tick after rebuilding the entity snapshot so
 * scripts see up-to-date entity state.
 *
 * @param rt   Runtime. Must not be NULL.
 * @param view Entity snapshot view. May be NULL.
 */
void aegis_script_runtime_set_entity_view(
    aegis_script_runtime_t *rt,
    const struct script_entity_view *view);

/**
 * @brief Register a compiled script for lazy spawning.
 *
 * Stores the bytecode in the registry. The script is NOT started yet.
 * When an event matching the script's topic_hash arrives, the script
 * is lazily spawned (loaded + fiber dispatched) and the event delivered.
 *
 * @param rt   Runtime. Must not be NULL.
 * @param name Debug name (copied, truncated to 63 chars).
 * @param bc   Compiled bytecode (instructions are copied).
 * @return Registry index on success, AEGIS_SCRIPT_ID_INVALID on failure.
 *
 * Side effects: copies bytecode instructions into owned storage.
 */
uint32_t aegis_script_runtime_register(aegis_script_runtime_t *rt,
                                       const char *name,
                                       const aegis_bytecode_t *bc);

/**
 * @brief Unregister a script by name.
 *
 * Removes from registry. If the script was already spawned, also
 * unloads the active instance.
 *
 * @param rt   Runtime. Must not be NULL.
 * @param name Script name to unregister.
 * @return true if found and removed, false if not found.
 */
bool aegis_script_runtime_unregister(aegis_script_runtime_t *rt,
                                     const char *name);

/**
 * @brief Find a registered script entry by name.
 *
 * @param rt   Runtime. Must not be NULL.
 * @param name Script name to find.
 * @return Pointer to entry if found, NULL otherwise. Not owned by caller.
 */
const aegis_script_entry_t *aegis_script_runtime_find(
    const aegis_script_runtime_t *rt, const char *name);

/**
 * @brief Clear the entire script registry and unload all active scripts.
 *
 * Used when loading a new level.
 *
 * @param rt Runtime. Must not be NULL.
 */
void aegis_script_runtime_clear_registry(aegis_script_runtime_t *rt);

/**
 * @brief Tick idle tracking — auto-unschedule scripts that exited and
 *        haven't received a new event within the grace window.
 *
 * Call once per server tick from the main thread.
 *
 * @param rt Runtime. Must not be NULL.
 */
void aegis_runtime_tick_idle(aegis_script_runtime_t *rt);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_AEGIS_RUNTIME_H */

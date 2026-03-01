/**
 * @file aegis_config.h
 * @brief Aegis VM configuration parameters.
 *
 * Per ref/aegis_bytecode_spec.md §3.6 and §6.
 *
 * Controls arena sizing, fuel budget, wall-time limits, and
 * per-script resource caps.
 */

#ifndef FERRUM_AEGIS_CONFIG_H
#define FERRUM_AEGIS_CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief VM instance configuration.
 *
 * All sizes are in bytes unless otherwise noted.
 * The arena is split into three zones: [static | stack | heap].
 * arena_size >= static_max + stack_max must hold.
 */
typedef struct aegis_config {
    /** Total arena size per script instance (bytes). Default: 64 KB. */
    uint32_t arena_size;

    /** Maximum static array size (bytes). Default: 4 KB. */
    uint32_t static_max;

    /** Maximum call stack size (bytes). Default: 4 KB. */
    uint32_t stack_max;

    /** Fuel budget per resume (instruction count). Default: 10000. */
    uint32_t fuel_budget;

    /** Wall-time budget per resume (nanoseconds). Default: 1 ms. */
    uint64_t wall_time_budget_ns;

    /** Maximum state updates per yield. Default: 64. */
    uint32_t max_updates;

    /** Maximum concurrent async tasks per script. Default: 8. */
    uint32_t max_async_tasks;
} aegis_config_t;

/**
 * @brief Return a configuration with sensible defaults.
 *
 * Defaults:
 *   arena_size:          65536  (64 KB)
 *   static_max:           4096  (4 KB)
 *   stack_max:            4096  (4 KB)
 *   fuel_budget:         10000  instructions
 *   wall_time_budget_ns: 1000000 (1 ms)
 *   max_updates:            64
 *   max_async_tasks:         8
 *
 * Side effects: none. Pure function.
 */
static inline aegis_config_t aegis_config_default(void) {
    aegis_config_t cfg;
    cfg.arena_size          = 65536;
    cfg.static_max          = 4096;
    cfg.stack_max           = 4096;
    cfg.fuel_budget         = 10000;
    cfg.wall_time_budget_ns = 1000000;
    cfg.max_updates         = 64;
    cfg.max_async_tasks     = 8;
    return cfg;
}

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_AEGIS_CONFIG_H */

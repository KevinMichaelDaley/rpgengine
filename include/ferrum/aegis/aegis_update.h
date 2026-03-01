/**
 * @file aegis_update.h
 * @brief Aegis update set types: state updates produced by scripts.
 *
 * Per ref/aegis_bytecode_spec.md §2.3.
 *
 * Scripts accumulate aegis_state_update_t entries via build_update,
 * target_entity, set_field, add_hint, push_update instructions.
 * The update set is returned to the engine on yield.
 *
 * Types exposed:
 *   - aegis_state_update_t  — single attribute mutation
 *   - aegis_update_set_t    — collection of mutations (pre-allocated)
 */

#ifndef FERRUM_AEGIS_UPDATE_H
#define FERRUM_AEGIS_UPDATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------------- */
/* Validation hint flags                                                    */
/* ----------------------------------------------------------------------- */

/** Movement hint: "this is movement, not teleport". */
#define AEGIS_HINT_MOVEMENT   (1u << 0)

/** Authority hint: "server-authoritative calculation". */
#define AEGIS_HINT_AUTHORITY  (1u << 1)

/** Prediction hint: "client prediction, verify tolerance". */
#define AEGIS_HINT_PREDICTION (1u << 2)

/* ----------------------------------------------------------------------- */
/* State update                                                             */
/* ----------------------------------------------------------------------- */

/**
 * @brief A single state mutation produced by a script.
 *
 * Carries target entity, attribute key, typed value, and validation hints.
 * Maps to script_env_write_attr() for applying via script_update_buffer_t.
 */
typedef struct aegis_state_update {
    uint32_t target;      /**< Target entity ID. */
    uint16_t key;         /**< Attribute key (SCRIPT_KEY_*). */
    uint8_t  type;        /**< Attribute type (SCRIPT_ATTR_*). */
    uint8_t  size;        /**< Payload size in bytes (max 16). */
    uint32_t hints;       /**< Validation hint flags (AEGIS_HINT_*). */
    uint8_t  value[16];   /**< Inline value (max 16 bytes = register size). */
} aegis_state_update_t;

/* ----------------------------------------------------------------------- */
/* Update set                                                               */
/* ----------------------------------------------------------------------- */

/** Default maximum updates per script per tick. */
#define AEGIS_MAX_UPDATES 1024

/**
 * @brief Collection of state mutations produced by a script execution.
 *
 * Pre-allocated with fixed capacity. Count grows as push_update appends.
 * Owned by the VM; reset on yield.
 */
typedef struct aegis_update_set {
    uint64_t trace_hash;   /**< Content-addressable execution trace. */
    uint64_t wall_time_ns; /**< Time spent executing. */
    uint32_t count;        /**< Number of updates appended. */
    uint32_t capacity;     /**< Maximum updates (pre-allocated). */
    aegis_state_update_t *updates; /**< Update array (owned). */
} aegis_update_set_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_AEGIS_UPDATE_H */

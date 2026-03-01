/**
 * @file aegis_ops_event.h
 * @brief Aegis event access instruction handlers.
 *
 * Per ref/aegis_bytecode_spec.md §3.3 (Event access group).
 *
 * Handlers read event metadata and payload fields from the current event
 * attached to the VM. All payload access is bounds-checked.
 *
 * Ownership: callers own all register pointers; event is read-only.
 * Nullability: event may be NULL (returns false / error).
 */

#ifndef FERRUM_AEGIS_OPS_EVENT_H
#define FERRUM_AEGIS_OPS_EVENT_H

#include <stdbool.h>
#include "ferrum/aegis/aegis_event.h"
#include "ferrum/aegis/aegis_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Load the event type hash into a register.
 *
 * @param dst   Destination register. Must not be NULL.
 * @param event Current event. Must not be NULL.
 * @return true on success, false if event is NULL.
 */
bool aegis_op_event_type(aegis_register_t *dst,
                         const aegis_event_t *event);

/**
 * @brief Load the event source entity ID into a register.
 *
 * @param dst   Destination register. Must not be NULL.
 * @param event Current event. Must not be NULL.
 * @return true on success, false if event is NULL.
 */
bool aegis_op_event_src(aegis_register_t *dst,
                        const aegis_event_t *event);

/**
 * @brief Load a typed field from the event payload.
 *
 * Reads `size` bytes from `payload[offset]` into the destination register.
 * Bounds-checked: offset + size must not exceed payload_len.
 *
 * @param dst    Destination register. Must not be NULL.
 * @param event  Current event. Must not be NULL.
 * @param offset Byte offset into payload.
 * @param size   Number of bytes to read (1, 2, 4, 8, 12, or 16).
 * @return true on success, false if event is NULL, offset+size > payload_len,
 *         or size > 16.
 */
bool aegis_op_event_field(aegis_register_t *dst,
                          const aegis_event_t *event,
                          uint32_t offset,
                          uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_AEGIS_OPS_EVENT_H */

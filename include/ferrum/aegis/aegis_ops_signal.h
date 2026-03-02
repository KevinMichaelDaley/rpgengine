/**
 * @file aegis_ops_signal.h
 * @brief Event signaling instruction handlers: signal, subscribe, await_event.
 *
 * These handlers let scripts communicate via the engine's event system:
 * - signal: publish a rate-limited event to the topic table.
 * - subscribe: wire the calling script to a topic for event delivery.
 * - await_event: wait-yield until a matching topic event arrives.
 *
 * Ownership: handlers borrow the VM and decoded instruction. The VM
 *   must have topic_table, event_queue, and script_id set by the runtime.
 * Nullability: vm and d must not be NULL.
 * Error semantics: signal/subscribe always succeed (status in dst reg).
 *   await_event returns false when no matching event (caller should wait-yield).
 */
#ifndef FERRUM_AEGIS_OPS_SIGNAL_H
#define FERRUM_AEGIS_OPS_SIGNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

struct aegis_vm;
struct aegis_decode_result;

/**
 * @brief Signal an event to the server (signal r_dst, r_topic_hash, r_payload).
 *
 * Rate-limited per script. Builds an aegis_event_t from the topic hash
 * and payload register, publishes via the topic table if not throttled.
 * Writes integer status code to r_dst:
 *   0 = success, 1 = rate-limited, 2 = invalid topic, 3 = queue full.
 *
 * @return true always (status reported in register).
 */
bool aegis_op_signal(struct aegis_vm *vm, const struct aegis_decode_result *d);

/**
 * @brief Subscribe the calling script to a topic (subscribe r_dst, r_topic_hash).
 *
 * Wires into aegis_topic_subscribe(). Status in r_dst:
 *   0 = success, 1 = already subscribed, 2 = table full.
 *
 * @return true always (status reported in register).
 */
bool aegis_op_subscribe(struct aegis_vm *vm, const struct aegis_decode_result *d);

/**
 * @brief Wait for a matching event (await_event r_dst, r_topic_hash).
 *
 * Scans the script's event queue for an event whose type matches the
 * topic hash. If found, pops it and packs type + source + first 8
 * payload bytes into r_dst (16 bytes). Returns true (advance PC).
 * If no match, returns false (caller should wait-yield, do not advance PC).
 *
 * @return true if event found, false if no match (wait-yield).
 */
bool aegis_op_await_event(struct aegis_vm *vm, const struct aegis_decode_result *d);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_AEGIS_OPS_SIGNAL_H */

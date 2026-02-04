/**
 * @file state_update_queue.h
 * @brief Global state update queue bridging network IO/client fibers to simulation jobs.
 */
#ifndef FERRUM_SERVER_NET_STATE_UPDATE_QUEUE_H
#define FERRUM_SERVER_NET_STATE_UPDATE_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Configuration for the state update queue.
 *
 * Ownership semantics:
 * - `fr_state_update_queue_push` copies `payload_size` bytes from `payload`.
 * - `fr_state_update_queue_pop` copies payload bytes into the caller buffer.
 */
typedef struct fr_state_update_queue_config_t {
    /** Maximum number of enqueued updates. 0 -> default (1024). */
    uint32_t capacity;
    /** Maximum payload size (bytes) allowed per update. 0 -> default (1024). */
    uint32_t max_payload_size;
} fr_state_update_queue_config_t;

/** Opaque queue handle. */
typedef struct fr_state_update_queue fr_state_update_queue_t;

/**
 * Create a queue.
 *
 * Thread-safety:
 * - Multi-producer safe.
 * - Multi-consumer safe.
 * - Operations are non-blocking with respect to empty/full conditions (return false).
 *   Internal locking is used for short critical sections.
 *
 * @param cfg Optional configuration; when NULL, defaults are used.
 * @return Queue handle on success, or NULL on allocation/parameter failure.
 */
fr_state_update_queue_t *fr_state_update_queue_create(const fr_state_update_queue_config_t *cfg);

/** Destroy a queue and free any queued updates. NULL-safe. */
void fr_state_update_queue_destroy(fr_state_update_queue_t *q);

/**
 * Push a decoded update into the queue.
 *
 * @param q Queue handle.
 * @param client_id Source client id.
 * @param schema_id Message/command schema id.
 * @param payload Payload bytes to copy.
 * @param payload_size Payload size in bytes.
 * @return true on success; false if full, invalid args, size exceeds max, or OOM.
 */
bool fr_state_update_queue_push(fr_state_update_queue_t *q,
                               uint16_t client_id,
                               uint16_t schema_id,
                               const uint8_t *payload,
                               uint16_t payload_size);

/**
 * Pop the next queued update.
 *
 * @param q Queue handle.
 * @param out_client_id Receives client id.
 * @param out_schema_id Receives schema id.
 * @param out_payload Destination buffer for payload bytes.
 * @param inout_payload_size In: capacity of `out_payload` in bytes. Out: payload size.
 * @return true if an update was popped; false if empty or args invalid or buffer too small.
 */
bool fr_state_update_queue_pop(fr_state_update_queue_t *q,
                              uint16_t *out_client_id,
                              uint16_t *out_schema_id,
                              uint8_t *out_payload,
                              uint16_t *inout_payload_size);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_SERVER_NET_STATE_UPDATE_QUEUE_H */

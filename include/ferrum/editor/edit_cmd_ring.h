/**
 * @file edit_cmd_ring.h
 * @brief Lock-free SPSC ring buffer for editor command/response passing.
 *
 * Used to bridge the editor I/O thread (producer) and the main tick thread
 * (consumer) without locks. Also used in reverse for responses.
 *
 * Stores fixed-size message slots. Each slot contains a header (id + length)
 * and a payload buffer. The ring is lock-free using atomic load/store with
 * appropriate memory ordering.
 */
#ifndef FERRUM_EDITOR_EDIT_CMD_RING_H
#define FERRUM_EDITOR_EDIT_CMD_RING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

/* ------------------------------------------------------------------------ */
/* Configuration                                                             */
/* ------------------------------------------------------------------------ */

/** @brief Maximum payload size per ring slot (1 MB). */
#define EDIT_CMD_RING_MAX_PAYLOAD  (1024 * 1024)

/** @brief Default ring capacity (number of slots). */
#define EDIT_CMD_RING_DEFAULT_CAP  1024

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief A single slot in the command ring.
 *
 * Fixed-size header + variable-length payload stored in a pre-allocated
 * buffer. Only payload_len bytes of payload are valid.
 */
typedef struct edit_cmd_slot {
    uint32_t id;            /**< Request/response correlation ID. */
    uint32_t payload_len;   /**< Length of payload in bytes. */
    char    *payload;       /**< Pointer into the slot's payload buffer. */
} edit_cmd_slot_t;

/**
 * @brief Lock-free SPSC ring buffer for editor commands or responses.
 *
 * The producer (I/O thread) calls push; the consumer (tick thread) calls pop.
 * Both indices are cache-line padded to avoid false sharing.
 *
 * Ownership:
 * - The caller owns the backing memory (slots array and payload buffers).
 * - init allocates via malloc; destroy frees.
 *
 * Thread safety:
 * - Exactly one producer thread and one consumer thread.
 * - No mutexes; uses atomic_uint with release/acquire ordering.
 */
typedef struct edit_cmd_ring {
    edit_cmd_slot_t *slots;         /**< Array of slot headers. */
    char            *payload_buf;   /**< Contiguous payload storage. */
    uint32_t         capacity;      /**< Number of slots (power of 2). */
    uint32_t         payload_cap;   /**< Max payload per slot. */
    uint32_t         mask;          /**< capacity - 1 (for fast modulo). */

    /* Cache-line separated to prevent false sharing. */
    _Alignas(64) atomic_uint head;  /**< Next write position (producer). */
    _Alignas(64) atomic_uint tail;  /**< Next read position (consumer). */
} edit_cmd_ring_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize a command ring with the given capacity.
 *
 * Capacity is rounded up to the next power of 2. Allocates slot headers
 * and payload buffers via malloc.
 *
 * @param ring         Ring to initialize.
 * @param capacity     Desired number of slots (will be rounded up to power of 2).
 * @param max_payload  Maximum payload per slot in bytes.
 * @return true on success, false on allocation failure.
 */
bool edit_cmd_ring_init(edit_cmd_ring_t *ring, uint32_t capacity,
                        uint32_t max_payload);

/**
 * @brief Free all memory owned by the ring.
 * @param ring  Ring to destroy.
 */
void edit_cmd_ring_destroy(edit_cmd_ring_t *ring);

/* ------------------------------------------------------------------------ */
/* Producer API (I/O thread)                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Push a command into the ring.
 *
 * Copies the payload into the ring's internal buffer. Thread-safe for a
 * single producer.
 *
 * @param ring         Ring to push into.
 * @param id           Correlation ID.
 * @param payload      Payload data to copy.
 * @param payload_len  Length of payload in bytes. Must be ≤ max_payload.
 * @return true if pushed, false if ring is full or payload too large.
 */
bool edit_cmd_ring_push(edit_cmd_ring_t *ring, uint32_t id,
                        const char *payload, uint32_t payload_len);

/* ------------------------------------------------------------------------ */
/* Consumer API (tick thread)                                                */
/* ------------------------------------------------------------------------ */

/**
 * @brief Peek at the next command without removing it.
 *
 * Returns a pointer to the next slot, or NULL if the ring is empty.
 * The slot and its payload remain valid until edit_cmd_ring_advance()
 * is called. The consumer must call advance() when done processing.
 *
 * @param ring  Ring to peek from.
 * @return Pointer to the next slot, or NULL if empty.
 */
const edit_cmd_slot_t *edit_cmd_ring_peek(edit_cmd_ring_t *ring);

/**
 * @brief Advance the consumer past the peeked slot.
 *
 * Must only be called after a successful peek(). Makes the slot
 * available for the producer to reuse.
 *
 * @param ring  Ring to advance.
 */
void edit_cmd_ring_advance(edit_cmd_ring_t *ring);

/**
 * @brief Convenience: peek + copy + advance in one call.
 *
 * Copies slot header and payload data into *out and the caller's
 * buffer, then advances. The payload is copied up to payload_buf_cap
 * bytes; out->payload_len reflects the actual length.
 * out->payload is set to payload_buf.
 *
 * @param ring            Ring to pop from.
 * @param out             Output slot header (id, payload_len).
 * @param payload_buf     Caller-owned buffer to copy payload into.
 * @param payload_buf_cap Size of caller's payload buffer.
 * @return true if a command was available, false if ring is empty.
 */
bool edit_cmd_ring_pop(edit_cmd_ring_t *ring, edit_cmd_slot_t *out,
                       char *payload_buf, uint32_t payload_buf_cap);

/* ------------------------------------------------------------------------ */
/* Query                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief Return the number of items currently in the ring.
 * @param ring  Ring to query.
 * @return Number of items (may be stale by the time caller uses it).
 */
uint32_t edit_cmd_ring_count(const edit_cmd_ring_t *ring);

/**
 * @brief Check if the ring is empty.
 * @param ring  Ring to query.
 * @return true if empty.
 */
bool edit_cmd_ring_empty(const edit_cmd_ring_t *ring);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_CMD_RING_H */

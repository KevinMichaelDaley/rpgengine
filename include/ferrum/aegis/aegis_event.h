/**
 * @file aegis_event.h
 * @brief Aegis VM event types, queue, and topic subscription system.
 *
 * Per ref/aegis_bytecode_spec.md §2.1, §2.2.
 *
 * Events route typed notifications from engine subsystems (physics,
 * entity lifecycle, gameplay triggers) to subscribed script instances.
 * Topic names use the `!` prefix convention (e.g., `!hit`, `!behave`).
 *
 * Types exposed:
 *   - aegis_event_t         — single event with type hash, source, tick, payload
 *   - aegis_event_queue_t   — per-script FIFO ring buffer of events
 *   - aegis_topic_table_t   — topic → subscriber list routing table
 *     (forward-declared; defined here as it shares the subscription API)
 */

#ifndef FERRUM_AEGIS_EVENT_H
#define FERRUM_AEGIS_EVENT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================= */
/* Event (§2.2)                                                             */
/* ======================================================================= */

/** Maximum payload size in bytes for an inline event. */
#define AEGIS_EVENT_MAX_PAYLOAD 64

/**
 * @brief A single typed event routed to scripts.
 *
 * Ownership: events are value-copied into per-script queues.
 * Nullability: all fields are valid after initialization.
 * Error semantics: none (POD type).
 * Side effects: none.
 */
typedef struct aegis_event {
    uint32_t type;        /**< Topic hash (from aegis_topic_hash()). */
    uint32_t source;      /**< Entity ID that triggered the event. */
    uint32_t tick;        /**< Server tick when the event was emitted. */
    uint32_t payload_len; /**< Bytes used in payload (0..AEGIS_EVENT_MAX_PAYLOAD). */
    uint8_t  payload[AEGIS_EVENT_MAX_PAYLOAD]; /**< Schema-validated event data. */
} aegis_event_t;

/* ======================================================================= */
/* Event queue (per-script ring buffer)                                     */
/* ======================================================================= */

/**
 * @brief Per-script FIFO event queue implemented as a ring buffer.
 *
 * When full, pushing drops the oldest event to make room.
 *
 * Ownership: the queue owns its internal buffer (allocated via malloc).
 * Nullability: must call aegis_event_queue_init() before use.
 * Error semantics: pop returns false when empty.
 * Side effects: init/destroy allocate/free heap memory.
 */
typedef struct aegis_event_queue {
    aegis_event_t *buf;   /**< Ring buffer storage. Owned by queue. */
    uint32_t       cap;   /**< Maximum number of events. */
    uint32_t       head;  /**< Next read position. */
    uint32_t       tail;  /**< Next write position. */
    uint32_t       count; /**< Current number of events in queue. */
} aegis_event_queue_t;

/**
 * @brief Initialize an event queue with the given capacity.
 *
 * @param q        Queue to initialize. Must not be NULL.
 * @param capacity Maximum number of events the queue can hold.
 *                 Must be > 0.
 *
 * Ownership: caller owns q; queue owns internal buffer until destroy.
 * Side effects: allocates heap memory for the buffer.
 */
void aegis_event_queue_init(aegis_event_queue_t *q, uint32_t capacity);

/**
 * @brief Destroy an event queue, freeing its internal buffer.
 *
 * @param q Queue to destroy. Must not be NULL. Safe to call on
 *          already-destroyed queue (no-op if buf is NULL).
 *
 * Side effects: frees heap memory.
 */
void aegis_event_queue_destroy(aegis_event_queue_t *q);

/**
 * @brief Push an event onto the queue.
 *
 * If the queue is full, drops the oldest event to make room.
 *
 * @param q  Queue. Must not be NULL, must be initialized.
 * @param ev Event to push (value-copied). Must not be NULL.
 * @return true always (overflow drops oldest, never fails).
 *
 * Side effects: may advance head if queue was full.
 */
bool aegis_event_queue_push(aegis_event_queue_t *q, const aegis_event_t *ev);

/**
 * @brief Pop the oldest event from the queue.
 *
 * @param q   Queue. Must not be NULL, must be initialized.
 * @param out Receives the popped event. Must not be NULL.
 * @return true if an event was popped, false if queue was empty.
 *
 * Side effects: none beyond advancing read pointer.
 */
bool aegis_event_queue_pop(aegis_event_queue_t *q, aegis_event_t *out);

/**
 * @brief Return the number of events currently in the queue.
 *
 * @param q Queue. Must not be NULL, must be initialized.
 * @return Event count (0..capacity).
 */
static inline uint32_t aegis_event_queue_count(const aegis_event_queue_t *q) {
    return q->count;
}

/* ======================================================================= */
/* Topic table (topic → subscriber routing)                                 */
/* ======================================================================= */

/**
 * @brief A single subscription entry: one script subscribing to one topic.
 *
 * Used internally by the topic table. Not part of the public API.
 */
typedef struct aegis_topic_sub {
    uint32_t topic_hash; /**< Topic the script is subscribed to. */
    uint32_t script_id;  /**< Script instance index. */
} aegis_topic_sub_t;

/**
 * @brief Topic routing table mapping topic hashes to subscriber lists.
 *
 * Uses a flat array of subscription entries. For the expected number of
 * scripts (< 256) and topics (< 64), linear scans are fast enough.
 *
 * Ownership: table owns its internal subscription array.
 * Nullability: must call aegis_topic_table_init() before use.
 * Error semantics: subscribe/unsubscribe return false on failure.
 * Side effects: init/destroy allocate/free heap memory.
 */
typedef struct aegis_topic_table {
    aegis_topic_sub_t *subs;     /**< Flat subscription array. Owned. */
    uint32_t           count;    /**< Number of active subscriptions. */
    uint32_t           capacity; /**< Max subscriptions. */
} aegis_topic_table_t;

/**
 * @brief Compute a topic hash from a topic name string.
 *
 * Topic names conventionally start with '!' (e.g., "!hit", "!behave").
 *
 * @param name Null-terminated topic name. Must not be NULL.
 * @return Non-zero 32-bit hash.
 *
 * Side effects: none. Pure function.
 */
uint32_t aegis_topic_hash(const char *name);

/**
 * @brief Initialize a topic routing table.
 *
 * @param table    Table to initialize. Must not be NULL.
 * @param max_subs Maximum total subscriptions (all topics × all scripts).
 * @param max_scripts Maximum number of scripts (unused, reserved).
 *
 * Ownership: caller owns table; table owns internal buffer until destroy.
 * Side effects: allocates heap memory.
 */
void aegis_topic_table_init(aegis_topic_table_t *table,
                            uint32_t max_subs,
                            uint32_t max_scripts);

/**
 * @brief Destroy a topic table, freeing its internal buffer.
 *
 * @param table Table to destroy. Must not be NULL.
 *
 * Side effects: frees heap memory.
 */
void aegis_topic_table_destroy(aegis_topic_table_t *table);

/**
 * @brief Subscribe a script to a topic.
 *
 * Duplicate subscriptions (same script + same topic) are rejected.
 *
 * @param table      Routing table. Must not be NULL.
 * @param topic_hash Topic hash (from aegis_topic_hash()).
 * @param script_id  Script instance index.
 * @return true on success, false if duplicate or table full.
 *
 * Side effects: modifies subscription array.
 */
bool aegis_topic_subscribe(aegis_topic_table_t *table,
                           uint32_t topic_hash,
                           uint32_t script_id);

/**
 * @brief Unsubscribe a script from a topic.
 *
 * @param table      Routing table. Must not be NULL.
 * @param topic_hash Topic hash.
 * @param script_id  Script instance index.
 * @return true if removed, false if subscription not found.
 *
 * Side effects: modifies subscription array.
 */
bool aegis_topic_unsubscribe(aegis_topic_table_t *table,
                             uint32_t topic_hash,
                             uint32_t script_id);

/**
 * @brief Publish an event to all subscribers of its topic.
 *
 * Routes the event to each subscribed script's queue. Scripts whose
 * script_id >= queue_count are silently skipped.
 *
 * @param table       Routing table. Must not be NULL.
 * @param ev          Event to publish. ev->type is the topic hash.
 * @param queues      Array of per-script event queues.
 * @param queue_count Number of queues in the array.
 *
 * Side effects: pushes event copies into subscriber queues.
 */
void aegis_topic_publish(const aegis_topic_table_t *table,
                         const aegis_event_t *ev,
                         aegis_event_queue_t *queues,
                         uint32_t queue_count);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_AEGIS_EVENT_H */

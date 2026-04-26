/**
 * @file aegis_sense.h
 * @brief SENSE_QUERY result types and constants.
 *
 * Per rpg-llm02a: SENSE_QUERY async opcode result layout.
 */

#ifndef FERRUM_AEGIS_SENSE_H
#define FERRUM_AEGIS_SENSE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ── Sense detection flags (upper 16 bits of r_mode_flags) ─────── */

#define AEGIS_SENSE_LOS        (1u << 0) /**< Line-of-sight (raycast). */
#define AEGIS_SENSE_PROXIMITY  (1u << 1) /**< Within spatial range. */
#define AEGIS_SENSE_AUDIO      (1u << 2) /**< Audible events/entities. */
#define AEGIS_SENSE_SMELL      (1u << 3) /**< Scent detection. */
#define AEGIS_SENSE_SHADOW     (1u << 4) /**< Shadow coverage. */

/* ── Query modes (lower 16 bits of r_mode_flags) ───────────────── */

#define AEGIS_SENSE_MODE_FULL      0 /**< Full sweep of all entities in range. */
#define AEGIS_SENSE_MODE_TARGETED  1 /**< Query data for single target entity only. */

/* ── Entity visibility flags ───────────────────────────────────── */

#define AEGIS_SENSE_ENTITY_VISIBLE  (1u << 0)
#define AEGIS_SENSE_ENTITY_AUDIBLE  (1u << 1)
#define AEGIS_SENSE_ENTITY_SMELLED  (1u << 2)

/* ── Result structures ─────────────────────────────────────────── */

/**
 * @brief Header written at the start of the SENSE_QUERY result buffer.
 *
 * Followed by entity_count × aegis_sense_entity_t,
 * then event_count  × aegis_sense_event_t.
 *
 * If truncated is non-zero, total_found > entity_count and the
 * caller should issue additional paginated queries to retrieve all.
 */
typedef struct aegis_sense_result {
    int32_t  status;          /**< 0 = ok, -1 = error. */
    uint32_t entity_count;    /**< Entities written to this buffer. */
    uint32_t event_count;     /**< Events written to this buffer. */
    uint32_t total_found;     /**< Total entities found before truncation. */
    uint32_t truncated;       /**< Non-zero if result was truncated. */
} aegis_sense_result_t;

/**
 * @brief Per-entity sense data.
 *
 * The name field is a flexible array member (null-terminated).
 * Use aegis_sense_entity_size() to compute total bytes.
 */
typedef struct aegis_sense_entity {
    uint32_t entity_id;       /**< ECS entity index (from phys_body::entity_index). */
    float    distance;        /**< Distance from querier to entity (meters). */
    float    salience;        /**< 0.0–1.0 composite score (closer = higher). */
    uint16_t flags;           /**< AEGIS_SENSE_ENTITY_* bitmask. */
    char     name[1];         /**< Null-terminated display name (may be empty). */
} aegis_sense_entity_t;

/**
 * @brief Per-event sense data.
 *
 * The description field is a flexible array member.
 * Use aegis_sense_event_size() to compute total bytes.
 */
typedef struct aegis_sense_event {
    uint32_t event_type_hash; /**< Hash of event type string. */
    float    distance;        /**< Distance from querier to event source. */
    float    salience;        /**< 0.0–1.0 composite score. */
    char     description[1];  /**< Null-terminated short text. */
} aegis_sense_event_t;

/* ── Inline size helpers ───────────────────────────────────────── */

static inline uint32_t aegis_sense_entity_size(const char *name) {
    uint32_t len = 0;
    if (name) {
        while (name[len]) len++;
    }
    /* entity_id(4) + distance(4) + salience(4) + flags(2) + name[1](1) = 15 base */
    return 15 + len;
}

static inline uint32_t aegis_sense_event_size(const char *desc) {
    uint32_t len = 0;
    if (desc) {
        while (desc[len]) len++;
    }
    /* event_type_hash(4) + distance(4) + salience(4) + description[1](1) = 13 base */
    return 13 + len;
}

/* ── Constants ─────────────────────────────────────────────────── */

/** Pre-allocated result buffer size for SENSE_QUERY (4 KB). */
#define AEGIS_SENSE_RESULT_CAPACITY 4096

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_AEGIS_SENSE_H */

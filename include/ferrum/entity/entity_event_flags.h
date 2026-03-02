/**
 * @file entity_event_flags.h
 * @brief Well-known entity attribute key and bitmask flags for
 *        physics→scripting event opt-in.
 *
 * Entities with these flags set in their event_flags attribute will
 * receive corresponding collision / overlap events through the aegis
 * scripting event queue.
 *
 * The flags are stored as a uint32 attribute at key
 * ENTITY_ATTR_KEY_EVENT_FLAGS (SCRIPT_KEY_USER + 0).
 */
#ifndef FERRUM_ENTITY_EVENT_FLAGS_H
#define FERRUM_ENTITY_EVENT_FLAGS_H

#include "ferrum/entity/entity_attrs.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Attribute key for the entity event flags bitmask. */
#define ENTITY_ATTR_KEY_EVENT_FLAGS  (SCRIPT_KEY_USER + 0)

/** Entity receives "hit!" events on first-contact collision. */
#define ENTITY_EVENT_FLAG_HIT        (1u << 0)

/** Entity receives "overlap!" events on first interior overlap. */
#define ENTITY_EVENT_FLAG_OVERLAP    (1u << 1)

/**
 * @brief Check whether an entity has a specific event flag set.
 *
 * Reads the event_flags attribute from the entity's attribute store.
 *
 * @param attrs  Pointer to entity attribute store.
 * @param flag   Flag bitmask to test (e.g. ENTITY_EVENT_FLAG_HIT).
 * @return true if the flag is set, false otherwise or if attrs is NULL.
 */
static inline bool entity_has_event_flag(const entity_attrs_t *attrs,
                                         uint32_t flag) {
    if (!attrs) return false;
    uint8_t type = 0, size = 0;
    const void *data = entity_attrs_get(
        attrs, ENTITY_ATTR_KEY_EVENT_FLAGS, &type, &size);
    if (!data || size < sizeof(uint32_t)) return false;
    uint32_t val = 0;
    memcpy(&val, data, sizeof(uint32_t));
    return (val & flag) != 0;
}

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ENTITY_EVENT_FLAGS_H */

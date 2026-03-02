/**
 * @file entity_attrs.h
 * @brief Dynamic key-value attribute storage for entities.
 *
 * Provides a fixed-capacity attribute block that can be embedded in any
 * entity struct (editor edit_entity_t, ECS components, etc.). Attributes
 * are stored as a sorted directory of key→offset entries with packed
 * payload bytes, enabling O(log n) lookups by key.
 *
 * Thread safety: not thread-safe. Caller must synchronize access.
 * Ownership: the entity_attrs_t is value-typed (no heap allocation).
 * Nullability: all pointer parameters must be non-NULL unless documented.
 */
#ifndef FERRUM_ENTITY_ATTRS_H
#define FERRUM_ENTITY_ATTRS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------------ */
/* Attribute type tags                                                       */
/* ------------------------------------------------------------------------ */

/** @brief Attribute value types. */
enum {
    SCRIPT_ATTR_F32  = 0, /**< Single float (4 bytes). */
    SCRIPT_ATTR_VEC3 = 1, /**< float[3] (12 bytes). */
    SCRIPT_ATTR_I32  = 2, /**< int32_t (4 bytes). */
    SCRIPT_ATTR_U32  = 3, /**< uint32_t (4 bytes). */
    SCRIPT_ATTR_BOOL = 4, /**< uint8_t 0/1 (1 byte). */
    SCRIPT_ATTR_STR  = 5, /**< Null-terminated string. */
    SCRIPT_ATTR_BLOB = 6, /**< Raw bytes. */
};

/* ------------------------------------------------------------------------ */
/* Well-known attribute keys                                                 */
/* ------------------------------------------------------------------------ */

/** @brief Well-known attribute keys (extensible by gameplay code). */
enum {
    /* Core transform (both edit entities and ECS entities) */
    SCRIPT_KEY_POS       = 0,   /**< vec3: world position. */
    SCRIPT_KEY_ROT       = 1,   /**< vec3: euler rotation (degrees). */
    SCRIPT_KEY_SCALE     = 2,   /**< vec3: per-axis scale. */
    SCRIPT_KEY_NAME      = 3,   /**< str: display name. */
    SCRIPT_KEY_TYPE      = 4,   /**< u32: entity type ID. */
    SCRIPT_KEY_BODY_IDX  = 5,   /**< u32: physics body index. */
    SCRIPT_KEY_MATERIAL  = 6,   /**< str: material path (slot in high bits). */
    SCRIPT_KEY_MASS      = 7,   /**< f32: physics body mass (0 = no gravity). */
    SCRIPT_KEY_STATIC    = 8,   /**< bool: body is static (immovable). */
    SCRIPT_KEY_KINEMATIC = 9,   /**< bool: body is kinematic (velocity-driven). */
    SCRIPT_KEY_ANG_VEL   = 10,  /**< vec3: angular velocity (rad/s). */
    SCRIPT_KEY_LIN_VEL   = 11,  /**< vec3: linear velocity (m/s). */

    /* ECS component keys (mapped from registered sparse sets) */
    SCRIPT_KEY_ECS_BASE  = 64,  /**< ECS components start here. */

    /* User/gameplay attribute keys */
    SCRIPT_KEY_USER      = 256, /**< User-defined keys start here. */
};

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/** @brief Total byte budget for the dynamic attribute block. */
#define ENTITY_ATTRS_CAPACITY 2048

/**
 * @brief Maximum number of directory entries.
 *
 * The directory and payload share the ENTITY_ATTRS_CAPACITY budget.
 * Each directory entry is 8 bytes. We reserve a portion for the directory
 * and the rest for payload. Max entries is bounded so that even with
 * zero-size payloads we don't overflow.
 */
#define ENTITY_ATTRS_MAX_ENTRIES 128

/**
 * @brief A single directory entry mapping a key to its payload location.
 *
 * Entries are kept sorted by key for binary search.
 */
typedef struct attr_entry {
    uint16_t key;    /**< Attribute key (SCRIPT_KEY_* or user-defined). */
    uint8_t  type;   /**< Attribute type (SCRIPT_ATTR_*). */
    uint8_t  size;   /**< Payload size in bytes (max 255). */
    uint16_t offset; /**< Byte offset into the payload region. */
    uint16_t _pad;   /**< Padding for alignment. */
} attr_entry_t;

/**
 * @brief Dynamic key-value attribute block for an entity.
 *
 * Layout:
 *   - Header: count + used bytes (4 bytes)
 *   - Directory: attr_entry_t[ENTITY_ATTRS_MAX_ENTRIES]
 *   - Payload: packed attribute values
 *
 * The directory is sorted by key for O(log n) binary search lookups.
 * Total struct size is fixed at ENTITY_ATTRS_CAPACITY bytes so it
 * can be embedded directly in entity structs without heap allocation.
 *
 * Side effects: none (pure data container).
 * Error semantics: set returns false if out of space.
 */
typedef struct entity_attrs {
    uint16_t    count;   /**< Number of active directory entries. */
    uint16_t    used;    /**< Bytes used in payload region. */
    attr_entry_t entries[ENTITY_ATTRS_MAX_ENTRIES]; /**< Sorted directory. */
    /** Payload region: packed attribute values. */
    uint8_t     payload[ENTITY_ATTRS_CAPACITY
                        - sizeof(uint16_t) * 2
                        - sizeof(attr_entry_t) * ENTITY_ATTRS_MAX_ENTRIES];
} entity_attrs_t;

/* ------------------------------------------------------------------------ */
/* API (entity_attrs.c)                                                      */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize an attribute block to empty.
 * @param attrs  Attribute block to initialize. Must not be NULL.
 *
 * Side effects: zeroes the header fields. Does not clear payload.
 */
void entity_attrs_init(entity_attrs_t *attrs);

/**
 * @brief Set (insert or update) an attribute.
 *
 * If the key already exists, the payload is updated in-place (if same size)
 * or reallocated within the payload region (if different size). If the key
 * is new, a directory entry is inserted in sorted position and payload
 * is appended.
 *
 * @param attrs  Attribute block. Must not be NULL.
 * @param key    Attribute key.
 * @param type   Attribute type (SCRIPT_ATTR_*).
 * @param data   Pointer to value data. Must not be NULL if size > 0.
 * @param size   Payload size in bytes (max 255).
 * @return true on success, false if out of space (capacity or max entries).
 *
 * Ownership: data is copied into the attrs block.
 * Side effects: may compact payload on size-change updates.
 */
bool entity_attrs_set(entity_attrs_t *attrs, uint16_t key,
                      uint8_t type, const void *data, uint8_t size);

/**
 * @brief Get an attribute by key.
 *
 * @param attrs     Attribute block. Must not be NULL.
 * @param key       Attribute key to look up.
 * @param out_type  Output: attribute type. May be NULL if not needed.
 * @param out_size  Output: payload size. May be NULL if not needed.
 * @return Pointer to payload data within the attrs block, or NULL if
 *         the key is not present. The pointer is valid until the next
 *         mutating operation on this attrs block.
 *
 * Side effects: none (read-only).
 */
const void *entity_attrs_get(const entity_attrs_t *attrs, uint16_t key,
                             uint8_t *out_type, uint8_t *out_size);

/**
 * @brief Remove an attribute by key.
 *
 * Removes the directory entry and compacts remaining entries. Payload
 * space is reclaimed (all offsets adjusted).
 *
 * @param attrs  Attribute block. Must not be NULL.
 * @param key    Attribute key to remove.
 * @return true if the key was found and removed, false if not present.
 *
 * Side effects: compacts payload, invalidates prior get() pointers.
 */
bool entity_attrs_remove(entity_attrs_t *attrs, uint16_t key);

/**
 * @brief Remove all attributes.
 * @param attrs  Attribute block. Must not be NULL.
 *
 * Side effects: resets count and used to 0.
 */
void entity_attrs_clear(entity_attrs_t *attrs);

/**
 * @brief Count active attributes.
 * @param attrs  Attribute block. Must not be NULL.
 * @return Number of stored attributes.
 */
uint16_t entity_attrs_count(const entity_attrs_t *attrs);

/* ------------------------------------------------------------------------ */
/* Binary search (entity_attrs_search.c)                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief Binary search for a key in the sorted directory.
 *
 * @param entries  Sorted array of directory entries.
 * @param count    Number of entries.
 * @param key      Key to search for.
 * @param out_idx  Output: index where key was found or should be inserted.
 *                 Must not be NULL.
 * @return true if key was found at *out_idx, false if not found
 *         (*out_idx is the insertion point).
 */
bool entity_attrs_search(const attr_entry_t *entries, uint16_t count,
                         uint16_t key, uint16_t *out_idx);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ENTITY_ATTRS_H */

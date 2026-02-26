/**
 * @file json_parse.h
 * @brief Minimal JSON parser and serializer with arena-based allocation.
 *
 * Parses JSON from strings into a tree of json_value_t nodes allocated from
 * a caller-provided arena. Serializes json_value_t trees back to JSON text.
 * No dynamic allocation (malloc/free) is used internally.
 */
#ifndef FERRUM_EDITOR_JSON_PARSE_H
#define FERRUM_EDITOR_JSON_PARSE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/** @brief JSON value type tag. */
typedef enum json_type {
    JSON_NULL    = 0,
    JSON_BOOL    = 1,
    JSON_NUMBER  = 2,
    JSON_STRING  = 3,
    JSON_ARRAY   = 4,
    JSON_OBJECT  = 5,
} json_type_t;

/**
 * @brief A single JSON value (node in a JSON tree).
 *
 * All child nodes, strings, and key names are allocated from the arena passed
 * to json_parse(). The caller owns the arena lifetime.
 */
typedef struct json_value {
    json_type_t type;
    union {
        bool    boolean;       /**< JSON_BOOL */
        double  number;        /**< JSON_NUMBER */
        struct {
            const char *ptr;   /**< Not null-terminated in arena; use len. */
            uint32_t    len;
        } string;              /**< JSON_STRING */
        struct {
            struct json_value *items; /**< Array of child values. */
            uint32_t count;
        } array;               /**< JSON_ARRAY */
        struct {
            const char       **keys;    /**< Array of key strings. */
            uint32_t          *key_lens; /**< Lengths of each key. */
            struct json_value *vals;     /**< Array of value nodes. */
            uint32_t           count;
        } object;              /**< JSON_OBJECT */
    };
} json_value_t;

/* ------------------------------------------------------------------------ */
/* Arena (simple bump allocator for parse output)                            */
/* ------------------------------------------------------------------------ */

/**
 * @brief Simple bump-pointer arena for JSON parse allocations.
 *
 * The caller provides a pre-allocated buffer. json_parse() allocates all
 * nodes, strings, and arrays from this arena. No free/rewind is performed
 * internally — the caller frees the entire arena when done.
 */
typedef struct json_arena {
    uint8_t *buf;       /**< Base of pre-allocated buffer. */
    size_t   cap;       /**< Total capacity in bytes. */
    size_t   used;      /**< Current allocation offset. */
} json_arena_t;

/**
 * @brief Initialize a JSON arena with a pre-allocated buffer.
 * @param arena  Arena to initialize.
 * @param buf    Pre-allocated buffer.
 * @param cap    Size of buffer in bytes.
 */
void json_arena_init(json_arena_t *arena, void *buf, size_t cap);

/* ------------------------------------------------------------------------ */
/* Parsing                                                                   */
/* ------------------------------------------------------------------------ */

/**
 * @brief Parse a JSON string into a value tree.
 *
 * All allocations come from the provided arena. On failure, *out is zeroed.
 *
 * @param input  JSON text (need not be null-terminated; length is explicit).
 * @param len    Length of input in bytes.
 * @param arena  Arena to allocate from.
 * @param out    Output value. Set to JSON_NULL on failure.
 * @return true on success, false on parse error or arena exhaustion.
 */
bool json_parse(const char *input, size_t len, json_arena_t *arena,
                json_value_t *out);

/* ------------------------------------------------------------------------ */
/* Serialization                                                             */
/* ------------------------------------------------------------------------ */

/**
 * @brief Serialize a JSON value tree to a string buffer.
 *
 * Writes compact JSON (no extra whitespace). If the buffer is too small,
 * returns the number of bytes that *would* have been written (like snprintf).
 * Output is always null-terminated if cap > 0.
 *
 * @param val  Root value to serialize.
 * @param buf  Output buffer (may be NULL if cap == 0 for length query).
 * @param cap  Capacity of output buffer.
 * @return Number of bytes written (excluding null terminator), or the
 *         required size if cap was insufficient.
 */
size_t json_write(const json_value_t *val, char *buf, size_t cap);

/* ------------------------------------------------------------------------ */
/* Accessors (convenience helpers)                                           */
/* ------------------------------------------------------------------------ */

/**
 * @brief Look up a key in a JSON object.
 * @param obj  JSON value of type JSON_OBJECT.
 * @param key  Null-terminated key string.
 * @return Pointer to the value, or NULL if not found or obj is not an object.
 */
const json_value_t *json_object_get(const json_value_t *obj, const char *key);

/**
 * @brief Get an array element by index.
 * @param arr   JSON value of type JSON_ARRAY.
 * @param index Zero-based index.
 * @return Pointer to the element, or NULL if out of range or not an array.
 */
const json_value_t *json_array_get(const json_value_t *arr, uint32_t index);

/**
 * @brief Extract a null-terminated string from a JSON string value.
 *
 * Copies into caller buffer with null termination.
 *
 * @param val      JSON value of type JSON_STRING.
 * @param buf      Output buffer.
 * @param buf_cap  Capacity of output buffer.
 * @return true if copied successfully, false if not a string or truncated.
 */
bool json_string_copy(const json_value_t *val, char *buf, size_t buf_cap);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_JSON_PARSE_H */

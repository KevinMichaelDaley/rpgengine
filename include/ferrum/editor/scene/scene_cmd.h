/**
 * @file scene_cmd.h
 * @brief Scene editor command formatting and response parsing.
 *
 * Formats JSON commands for the edit server protocol and parses
 * JSON response lines. Commands follow the format:
 *   {"id":N,"cmd":"name","args":{...}}\n
 *
 * Public types: scene_cmd_response_t (1-type, under 2-type limit).
 */
#ifndef FERRUM_EDITOR_SCENE_CMD_H
#define FERRUM_EDITOR_SCENE_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Parsed command response from the editor server.
 *
 * Server responses: {"id":N,"ok":true,"result":...} or
 *                   {"id":N,"ok":false,"error":"code"}
 */
typedef struct scene_cmd_response {
    uint32_t id;                /**< Request ID from the command. */
    bool     ok;                /**< True if command succeeded. */
    double   result_number;     /**< Numeric result (e.g., spawned entity ID). */
    bool     result_bool;       /**< Boolean result (e.g., select success). */
    char     error[128];        /**< Error code string on failure. */
    bool     has_result;        /**< True if result field was present. */
    bool     result_is_number;  /**< True if result was a JSON number. */
} scene_cmd_response_t;

/* ---- Command formatting ---- */

/**
 * @brief Format a spawn command.
 * @param buf   Output buffer.
 * @param cap   Buffer capacity.
 * @param id    Request ID.
 * @param type  Entity type (EDIT_ENTITY_TYPE_*).
 * @param pos   World position [x, y, z].
 * @param name  Entity name (may be NULL for auto-naming).
 * @return Bytes written (excluding NUL), or -1 if buffer too small.
 */
int scene_cmd_format_spawn(char *buf, size_t cap, uint32_t id,
                           uint32_t type, const float pos[3],
                           const char *name);

/**
 * @brief Format a delete command (deletes selected entities).
 * @param buf   Output buffer.
 * @param cap   Buffer capacity.
 * @param id    Request ID.
 * @return Bytes written (excluding NUL), or -1 if buffer too small.
 */
int scene_cmd_format_delete(char *buf, size_t cap, uint32_t id);

/**
 * @brief Format a select command.
 * @param buf       Output buffer.
 * @param cap       Buffer capacity.
 * @param id        Request ID.
 * @param entity_id Entity to select.
 * @return Bytes written (excluding NUL), or -1 if buffer too small.
 */
int scene_cmd_format_select(char *buf, size_t cap, uint32_t id,
                            uint32_t entity_id);

/**
 * @brief Format a deselect command.
 * @param buf       Output buffer.
 * @param cap       Buffer capacity.
 * @param id        Request ID.
 * @param entity_id Entity to deselect.
 * @return Bytes written (excluding NUL), or -1 if buffer too small.
 */
int scene_cmd_format_deselect(char *buf, size_t cap, uint32_t id,
                              uint32_t entity_id);

/* ---- Transform + query commands (in scene_cmd_transform.c) ---- */

/**
 * @brief Format a list_entities command.
 * @param buf   Output buffer.
 * @param cap   Buffer capacity.
 * @param id    Request ID.
 * @return Bytes written (excluding NUL), or -1 if buffer too small.
 */
int scene_cmd_format_list(char *buf, size_t cap, uint32_t id);

/**
 * @brief Format a move command.
 * @param buf   Output buffer.
 * @param cap   Buffer capacity.
 * @param id    Request ID.
 * @param delta Translation delta [x, y, z].
 * @return Bytes written (excluding NUL), or -1 if buffer too small.
 */
int scene_cmd_format_move(char *buf, size_t cap, uint32_t id,
                          const float delta[3]);

/**
 * @brief Format a rotate command.
 * @param buf   Output buffer.
 * @param cap   Buffer capacity.
 * @param id    Request ID.
 * @param delta Rotation delta [x, y, z] in degrees.
 * @return Bytes written (excluding NUL), or -1 if buffer too small.
 */
int scene_cmd_format_rotate(char *buf, size_t cap, uint32_t id,
                            const float delta[3]);

/**
 * @brief Format a scale command.
 * @param buf    Output buffer.
 * @param cap    Buffer capacity.
 * @param id     Request ID.
 * @param factor Scale factor [x, y, z].
 * @return Bytes written (excluding NUL), or -1 if buffer too small.
 */
int scene_cmd_format_scale(char *buf, size_t cap, uint32_t id,
                           const float factor[3]);

/* ---- Response parsing (in scene_cmd_parse.c) ---- */

/**
 * @brief Parse a JSON response line from the server.
 * @param json  JSON string (not necessarily NUL-terminated).
 * @param len   Length of JSON string.
 * @param out   Parsed response output. Must not be NULL.
 * @return true if parsing succeeded, false on malformed input or NULL args.
 */
bool scene_cmd_parse_response(const char *json, size_t len,
                              scene_cmd_response_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_CMD_H */

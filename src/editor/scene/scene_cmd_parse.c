/**
 * @file scene_cmd_parse.c
 * @brief Parses JSON response lines from the editor server.
 *
 * Response format:
 *   Success: {"id":N,"ok":true,"result":VALUE}
 *   Error:   {"id":N,"ok":false,"error":"error_code"}
 *
 * Uses the project JSON parser with a stack-allocated arena (no malloc).
 */

#include "ferrum/editor/scene/scene_cmd.h"
#include "ferrum/editor/json_parse.h"

#include <string.h>

/** @brief Stack arena size for JSON parsing. */
#define PARSE_ARENA_SIZE 4096

/**
 * @brief Extract the "id" field from the parsed JSON object.
 * @param root  Parsed JSON object.
 * @param out   Response struct to populate.
 * @return true if "id" was found and is a number, false otherwise.
 */
static bool extract_id(const json_value_t *root, scene_cmd_response_t *out) {
    const json_value_t *id_val = json_object_get(root, "id");
    if (!id_val || id_val->type != JSON_NUMBER) {
        return false;
    }
    out->id = (uint32_t)id_val->number;
    return true;
}

/**
 * @brief Extract the "ok" field from the parsed JSON object.
 * @param root  Parsed JSON object.
 * @param out   Response struct to populate.
 * @return true if "ok" was found and is a boolean, false otherwise.
 */
static bool extract_ok(const json_value_t *root, scene_cmd_response_t *out) {
    const json_value_t *ok_val = json_object_get(root, "ok");
    if (!ok_val || ok_val->type != JSON_BOOL) {
        return false;
    }
    out->ok = ok_val->boolean;
    return true;
}

/**
 * @brief Extract the "result" field from a successful response.
 *
 * Sets result_number or result_bool depending on the JSON type.
 * Arrays and other types are noted via has_result but not stored
 * in the scalar fields.
 *
 * @param root  Parsed JSON object.
 * @param out   Response struct to populate.
 */
static void extract_result(const json_value_t *root,
                           scene_cmd_response_t *out) {
    const json_value_t *result_val = json_object_get(root, "result");
    if (!result_val) {
        return;
    }

    out->has_result = true;

    switch (result_val->type) {
    case JSON_NUMBER:
        out->result_number = result_val->number;
        out->result_is_number = true;
        break;
    case JSON_BOOL:
        out->result_bool = result_val->boolean;
        break;
    default:
        /* Arrays, objects, strings, null: has_result is set but no scalar. */
        break;
    }
}

/**
 * @brief Extract the "error" field from a failed response.
 * @param root  Parsed JSON object.
 * @param out   Response struct to populate.
 */
static void extract_error(const json_value_t *root,
                          scene_cmd_response_t *out) {
    const json_value_t *err_val = json_object_get(root, "error");
    if (!err_val || err_val->type != JSON_STRING) {
        return;
    }
    json_string_copy(err_val, out->error, sizeof(out->error));
}

bool scene_cmd_parse_response(const char *json, size_t len,
                              scene_cmd_response_t *out) {
    if (!json || !out) {
        return false;
    }

    memset(out, 0, sizeof(*out));

    /* Stack-allocated arena for JSON parsing — no dynamic allocation. */
    uint8_t arena_buf[PARSE_ARENA_SIZE];
    json_arena_t arena;
    json_arena_init(&arena, arena_buf, sizeof(arena_buf));

    json_value_t root;
    if (!json_parse(json, len, &arena, &root)) {
        return false;
    }

    /* Root must be an object. */
    if (root.type != JSON_OBJECT) {
        return false;
    }

    /* Extract required fields: "id" and "ok". */
    if (!extract_id(&root, out)) {
        return false;
    }
    if (!extract_ok(&root, out)) {
        return false;
    }

    /* Extract result or error depending on success/failure. */
    if (out->ok) {
        extract_result(&root, out);
    } else {
        extract_error(&root, out);
    }

    return true;
}

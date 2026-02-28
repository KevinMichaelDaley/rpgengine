/**
 * @file edit_cmd_resolve.c
 * @brief Entity ID resolution — number or name lookup.
 *
 * Non-static functions: edit_cmd_resolve_entity (1).
 */

#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/json_parse.h"
#include <stdlib.h>
#include <string.h>

uint32_t edit_cmd_resolve_entity(const edit_cmd_ctx_t *ctx,
                                 const json_value_t *id_val) {
    if (!ctx || !ctx->entities || !id_val) return EDIT_ENTITY_INVALID_ID;

    if (id_val->type == JSON_NUMBER) {
        /* Direct numeric ID. */
        return (uint32_t)id_val->number;
    }

    if (id_val->type == JSON_STRING && id_val->string.len > 0) {
        /* Name lookup — copy to null-terminated buffer. */
        char name[EDIT_ENTITY_NAME_MAX];
        uint32_t len = id_val->string.len;
        if (len >= sizeof(name)) len = sizeof(name) - 1;
        memcpy(name, id_val->string.ptr, len);
        name[len] = '\0';

        /* Try parsing as a number first (e.g., "42" as string). */
        char *end = NULL;
        unsigned long parsed = strtoul(name, &end, 10);
        if (end && *end == '\0' && end != name) {
            return (uint32_t)parsed;
        }

        /* Name lookup. */
        return edit_entity_store_find_by_name(ctx->entities, name);
    }

    return EDIT_ENTITY_INVALID_ID;
}

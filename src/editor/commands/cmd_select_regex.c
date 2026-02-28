/**
 * @file cmd_select_regex.c
 * @brief Select entities whose names match a POSIX regex pattern.
 *
 * JSON args: {"pattern": "wall.*"}
 * Selects all active entities whose name matches the pattern (case-insensitive).
 * Returns the number of entities selected.
 * Always succeeds (returns ok:true), even if zero matches.
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"

#include <regex.h>
#include <string.h>

bool cmd_select_regex(edit_dispatch_t *d, const json_value_t *args,
                      json_value_t *result, json_arena_t *arena) {
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !ctx->selection || !args) return false;

    /* Extract pattern string. */
    const json_value_t *pat_val = json_object_get(args, "pattern");
    if (!pat_val || pat_val->type != JSON_STRING || pat_val->string.len == 0) {
        return false;
    }

    /* Null-terminate pattern (arena strings may not be). */
    char pat_buf[256];
    uint32_t len = pat_val->string.len < 255 ? pat_val->string.len : 255;
    memcpy(pat_buf, pat_val->string.ptr, len);
    pat_buf[len] = '\0';

    /* Compile regex. */
    regex_t re;
    int rc = regcomp(&re, pat_buf, REG_EXTENDED | REG_NOSUB | REG_ICASE);
    if (rc != 0) {
        return false;
    }

    /* Iterate all entities, select those whose name matches. */
    uint32_t matched = 0;
    for (uint32_t i = 0; i < ctx->entities->capacity; i++) {
        const edit_entity_t *e = edit_entity_store_get(ctx->entities, i);
        if (!e) continue;
        if (e->name[0] == '\0') continue;  /* Skip unnamed entities. */

        if (regexec(&re, e->name, 0, NULL, 0) == 0) {
            edit_selection_add(ctx->selection, i);
            matched++;
        }
    }

    regfree(&re);

    /* Return count of matched entities. */
    result->type = JSON_NUMBER;
    result->number = (double)matched;
    return true;
}

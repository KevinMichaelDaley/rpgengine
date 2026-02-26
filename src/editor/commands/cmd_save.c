/**
 * @file cmd_save.c
 * @brief Save command — serialize entities to a JSON file.
 *
 * JSON args: {"path":"levels/test.json"}
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_serialize.h"

bool cmd_save(edit_dispatch_t *d, const json_value_t *args,
              json_value_t *result, json_arena_t *arena) {
    (void)result;
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !args) return false;

    /* Extract file path. */
    const json_value_t *path_val = json_object_get(args, "path");
    if (!path_val || path_val->type != JSON_STRING) return false;

    /* Copy path to null-terminated buffer. */
    char path[1024];
    if (!json_string_copy(path_val, path, sizeof(path))) return false;

    return edit_level_save(ctx->entities, path);
}

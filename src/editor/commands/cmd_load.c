/**
 * @file cmd_load.c
 * @brief Load command — deserialize entities from a JSON file.
 *
 * Clears the entity store and selection, then loads from file.
 * JSON args: {"path":"levels/test.json"}
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/edit_serialize.h"
#include "ferrum/editor/edit_undo.h"

bool cmd_load(edit_dispatch_t *d, const json_value_t *args,
              json_value_t *result, json_arena_t *arena) {
    (void)result;
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !args) return false;

    /* Extract file path. */
    const json_value_t *path_val = json_object_get(args, "path");
    if (!path_val || path_val->type != JSON_STRING) return false;

    char path[1024];
    if (!json_string_copy(path_val, path, sizeof(path))) return false;

    /* Clear selection and undo (loading is not undoable). */
    if (ctx->selection) edit_selection_clear(ctx->selection);
    if (ctx->undo) edit_undo_clear(ctx->undo);

    return edit_level_load(ctx->entities, path);
}

/**
 * @file cmd_undo_tree.c
 * @brief Undo tree command — displays branching undo history in TUI.
 *
 * JSON args: {} (no arguments)
 * Response: text string showing the undo tree structure.
 *
 * Non-static functions (1 / 4 limit):
 *   cmd_undo_tree
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_undo.h"
#include "ferrum/editor/undo_tree.h"
#include "ferrum/editor/undo_rebase.h"
#include <string.h>

/** @brief Static buffer for tree formatting (single-threaded tick thread). */
static char s_tree_buf[4096];

bool cmd_undo_tree(edit_dispatch_t *d, const json_value_t *args,
                   json_value_t *result, json_arena_t *arena) {
    (void)args;
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->undo) return false;

    uint32_t len = edit_undo_tree_format(
        ctx->undo, ctx->branches, s_tree_buf, sizeof(s_tree_buf));

    result->type       = JSON_STRING;
    result->string.ptr = s_tree_buf;
    result->string.len = len;
    return true;
}

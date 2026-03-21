/**
 * @file cmd_select_children.c
 * @brief Select all descendants of currently selected entities.
 *
 * JSON args: {} (operates on selection)
 * Adds all descendants (recursive DFS) to the selection set.
 *
 * Non-static functions (1 / 4 limit):
 *   cmd_select_children
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/edit_scene_tree.h"

bool cmd_select_children(edit_dispatch_t *d, const json_value_t *args,
                          json_value_t *result, json_arena_t *arena) {
    (void)args;
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !ctx->selection) return false;

    edit_scene_tree_t *tree = ctx->entities->tree;
    if (!tree) return false;

    uint32_t count = edit_selection_count(ctx->selection);
    if (count == 0) {
        result->type   = JSON_NUMBER;
        result->number = 0.0;
        return true;
    }

    /* Copy current selection IDs (selection may change during iteration). */
    const uint32_t *ids = edit_selection_ids(ctx->selection);
    uint32_t roots[256];
    uint32_t root_count = count < 256 ? count : 256;
    for (uint32_t i = 0; i < root_count; i++) {
        roots[i] = ids[i];
    }

    /* For each selected entity, DFS and add all descendants. */
    uint32_t added = 0;
    for (uint32_t r = 0; r < root_count; r++) {
        edit_scene_tree_iter_t it;
        edit_scene_tree_iter_init(&it, tree, roots[r]);

        uint32_t node;
        uint32_t depth;
        while (edit_scene_tree_iter_next(&it, &node, &depth)) {
            if (depth > 0) { /* Skip root itself (already selected). */
                if (!edit_selection_contains(ctx->selection, node)) {
                    edit_selection_add(ctx->selection, node);
                    added++;
                }
            }
        }
    }

    result->type   = JSON_NUMBER;
    result->number = (double)added;
    return true;
}

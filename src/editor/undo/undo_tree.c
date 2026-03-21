/**
 * @file undo_tree.c
 * @brief Format undo history as text tree for TUI display.
 *
 * Non-static functions (1 / 4 limit):
 *   edit_undo_tree_format
 */

#include "ferrum/editor/undo_tree.h"
#include "ferrum/editor/edit_undo.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/undo_rebase.h"
#include <stdio.h>
#include <string.h>

/** @brief Command type name for display. */
static const char *cmd_type_name_(uint32_t type) {
    switch (type) {
    case EDIT_CMD_TYPE_SPAWN:        return "spawn";
    case EDIT_CMD_TYPE_DELETE:       return "delete";
    case EDIT_CMD_TYPE_MOVE:         return "move";
    case EDIT_CMD_TYPE_ROTATE:       return "rotate";
    case EDIT_CMD_TYPE_SCALE:        return "scale";
    case EDIT_CMD_TYPE_GROUP_CREATE: return "group+";
    case EDIT_CMD_TYPE_GROUP_DELETE: return "group-";
    default:                         return "???";
    }
}

uint32_t edit_undo_tree_format(const edit_undo_stack_t *stack,
                                const undo_branches_t *branches,
                                char *buf, size_t cap) {
    if (!stack || !buf || cap == 0) return 0;

    size_t written = 0;
    int n;

    /* Header. */
    n = snprintf(buf + written, cap - written,
                 "Undo tree (%u entries, %u redo)\n",
                 edit_undo_count(stack), edit_undo_redo_count(stack));
    if (n > 0) written += (size_t)n;

    /* Walk active entries from base to top. */
    for (uint32_t i = stack->base; i < stack->top && written < cap - 1; i++) {
        uint32_t idx = i % stack->capacity;
        const edit_undo_entry_t *e = &stack->entries[idx];

        /* Mark cursor position. */
        const char *marker = (i == stack->cursor) ? ">> " :
                             (i < stack->cursor)  ? "   " : " . ";

        n = snprintf(buf + written, cap - written,
                     "%s[%u] %s e%u",
                     marker, i - stack->base,
                     cmd_type_name_(e->forward_type),
                     e->entity_id);
        if (n > 0) written += (size_t)n;

        /* Show group ID if grouped. */
        if (e->group_id != EDIT_UNDO_NO_GROUP) {
            n = snprintf(buf + written, cap - written,
                         " (g%u)", e->group_id);
            if (n > 0) written += (size_t)n;
        }

        n = snprintf(buf + written, cap - written, "\n");
        if (n > 0) written += (size_t)n;

        /* Check if any orphan branch diverges at this point. */
        if (branches) {
            for (uint32_t b = 0; b < undo_branches_count(branches); b++) {
                const undo_branch_t *br = undo_branches_get(branches, b);
                if (!br || br->diverge_id != i) continue;

                n = snprintf(buf + written, cap - written,
                             "       \\ orphan branch #%u (%u entries):\n",
                             b, br->count);
                if (n > 0) written += (size_t)n;

                for (uint32_t j = 0; j < br->count &&
                     written < cap - 1; j++) {
                    n = snprintf(buf + written, cap - written,
                                 "       | %s e%u\n",
                                 cmd_type_name_(br->entries[j].forward_type),
                                 br->entries[j].entity_id);
                    if (n > 0) written += (size_t)n;
                }
            }
        }
    }

    /* Mark cursor at end if it's at top. */
    if (stack->cursor == stack->top && written < cap - 1) {
        n = snprintf(buf + written, cap - written, ">> (HEAD)\n");
        if (n > 0) written += (size_t)n;
    }

    /* Orphan branch summary. */
    if (branches && undo_branches_count(branches) > 0 && written < cap - 1) {
        n = snprintf(buf + written, cap - written,
                     "\n%u orphan branch(es)\n",
                     undo_branches_count(branches));
        if (n > 0) written += (size_t)n;
    }

    return (uint32_t)written;
}

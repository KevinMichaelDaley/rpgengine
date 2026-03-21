/**
 * @file undo_tree.h
 * @brief Format undo history as a text tree for TUI display.
 *
 * Thread safety: read-only on stack and branches; safe from any thread.
 */
#ifndef FERRUM_EDITOR_UNDO_TREE_H
#define FERRUM_EDITOR_UNDO_TREE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* Forward declarations. */
struct edit_undo_stack;
struct undo_branches;

/**
 * @brief Format the undo history as a text tree.
 *
 * Writes a human-readable representation of the undo stack and
 * orphan branches into buf. The current cursor position is marked
 * with ">> " and orphan branches are shown inline at their
 * divergence points.
 *
 * @param stack     Undo stack.
 * @param branches  Orphan branch storage (NULL = no branches shown).
 * @param buf       Output buffer.
 * @param cap       Buffer capacity.
 * @return Number of bytes written (excluding NUL), or 0 on error.
 */
uint32_t edit_undo_tree_format(const struct edit_undo_stack *stack,
                                const struct undo_branches *branches,
                                char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_UNDO_TREE_H */

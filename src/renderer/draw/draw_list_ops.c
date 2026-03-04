/**
 * @file draw_list_ops.c
 * @brief Push and clear operations for draw_list_t.
 */

#include "ferrum/renderer/draw/draw_list.h"

#include <string.h>

/* ── draw_list_push ───────────────────────────────────────────────── */

int draw_list_push(draw_list_t *list, const draw_command_t *cmd)
{
    if (!list || !cmd) {
        return DRAW_LIST_ERR_INVALID;
    }
    if (list->count >= list->capacity) {
        return DRAW_LIST_ERR_FULL;
    }
    list->commands[list->count] = *cmd;
    ++list->count;
    return DRAW_LIST_OK;
}

/* ── draw_list_clear ──────────────────────────────────────────────── */

void draw_list_clear(draw_list_t *list)
{
    if (!list) { return; }
    list->count = 0;
}

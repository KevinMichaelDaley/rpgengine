/**
 * @file draw_list_init.c
 * @brief Lifecycle for draw_list_t: init and destroy.
 */

#include "ferrum/renderer/draw/draw_list.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* ── draw_list_init ───────────────────────────────────────────────── */

int draw_list_init(draw_list_t *list, uint32_t capacity)
{
    if (!list || capacity == 0) {
        return DRAW_LIST_ERR_INVALID;
    }

    /*
     * Allocate commands + scratch buffer for radix sort in one block.
     * Layout: [commands (capacity)] [scratch (capacity)]
     */
    size_t alloc_size = (size_t)capacity * 2 * sizeof(draw_command_t);
    draw_command_t *block = (draw_command_t *)malloc(alloc_size);
    if (!block) {
        return DRAW_LIST_ERR_OOM;
    }
    memset(block, 0, alloc_size);

    list->commands = block;
    list->count    = 0;
    list->capacity = capacity;
    return DRAW_LIST_OK;
}

/* ── draw_list_destroy ────────────────────────────────────────────── */

void draw_list_destroy(draw_list_t *list)
{
    if (!list || !list->commands) { return; }
    free(list->commands);
    list->commands = NULL;
    list->count    = 0;
    list->capacity = 0;
}

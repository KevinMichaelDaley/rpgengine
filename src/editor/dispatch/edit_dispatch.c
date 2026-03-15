/**
 * @file edit_dispatch.c
 * @brief Command dispatch table — lifecycle, registration, and lookup.
 */

#include "ferrum/editor/edit_dispatch.h"
#include <stdlib.h>
#include <string.h>

/** Default initial handler capacity. */
#define DISPATCH_INITIAL_CAPACITY 256

bool edit_dispatch_init(edit_dispatch_t *dispatch, size_t arena_size,
                        void *user_data) {
    if (!dispatch || arena_size == 0) return false;

    memset(dispatch, 0, sizeof(*dispatch));
    dispatch->user_data = user_data;

    dispatch->handlers = (edit_cmd_handler_entry_t *)calloc(
        DISPATCH_INITIAL_CAPACITY, sizeof(edit_cmd_handler_entry_t));
    if (!dispatch->handlers) return false;
    dispatch->handler_capacity = DISPATCH_INITIAL_CAPACITY;

    dispatch->parse_arena_buf = (uint8_t *)malloc(arena_size);
    dispatch->resp_arena_buf  = (uint8_t *)malloc(arena_size);
    if (!dispatch->parse_arena_buf || !dispatch->resp_arena_buf) {
        free(dispatch->handlers);
        free(dispatch->parse_arena_buf);
        free(dispatch->resp_arena_buf);
        return false;
    }
    dispatch->parse_arena_cap = arena_size;
    dispatch->resp_arena_cap  = arena_size;
    return true;
}

void edit_dispatch_destroy(edit_dispatch_t *dispatch) {
    if (!dispatch) return;
    free(dispatch->handlers);
    free(dispatch->parse_arena_buf);
    free(dispatch->resp_arena_buf);
    dispatch->handlers        = NULL;
    dispatch->parse_arena_buf = NULL;
    dispatch->resp_arena_buf  = NULL;
}

bool edit_dispatch_register(edit_dispatch_t *dispatch, const char *name,
                            edit_cmd_handler_fn handler) {
    if (!dispatch || !name || !handler || !dispatch->handlers) return false;

    /* Grow if needed, up to EDIT_DISPATCH_MAX_HANDLERS. */
    if (dispatch->handler_count >= dispatch->handler_capacity) {
        uint32_t new_cap = dispatch->handler_capacity * 2;
        if (new_cap > EDIT_DISPATCH_MAX_HANDLERS)
            new_cap = EDIT_DISPATCH_MAX_HANDLERS;
        if (dispatch->handler_count >= new_cap) return false;
        edit_cmd_handler_entry_t *new_buf = (edit_cmd_handler_entry_t *)realloc(
            dispatch->handlers, new_cap * sizeof(edit_cmd_handler_entry_t));
        if (!new_buf) return false;
        dispatch->handlers = new_buf;
        dispatch->handler_capacity = new_cap;
    }

    size_t name_len = strlen(name);
    if (name_len >= EDIT_DISPATCH_MAX_CMD_NAME) return false;

    edit_cmd_handler_entry_t *e = &dispatch->handlers[dispatch->handler_count];
    memcpy(e->name, name, name_len + 1);
    e->handler = handler;
    dispatch->handler_count++;
    return true;
}

edit_cmd_handler_fn edit_dispatch_lookup(const edit_dispatch_t *dispatch,
                                          const char *name, uint32_t name_len) {
    if (!dispatch || !name || !dispatch->handlers) return NULL;
    for (uint32_t i = 0; i < dispatch->handler_count; ++i) {
        const edit_cmd_handler_entry_t *e = &dispatch->handlers[i];
        if (strlen(e->name) == name_len &&
            memcmp(e->name, name, name_len) == 0) {
            return e->handler;
        }
    }
    return NULL;
}

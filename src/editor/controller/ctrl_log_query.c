/**
 * @file ctrl_log_query.c
 * @brief Log query operations: get entry, visible count, scroll.
 */

#include "ferrum/editor/ctrl_log.h"
#include <stddef.h>

const ctrl_log_entry_t *ctrl_log_get(const ctrl_log_t *log, uint32_t index) {
    if (!log || !log->entries || log->count == 0) return NULL;

    /* Available entries = min(count, capacity). */
    uint32_t avail = log->count < log->capacity ? log->count : log->capacity;

    /* Effective index: offset from newest by (index + scroll). */
    uint32_t offset = index + log->scroll;
    if (offset >= avail) return NULL;

    /* The newest entry is at (count-1) % capacity.
     * Entry at visual position 'offset' from newest is at:
     *   (count - 1 - offset) % capacity
     */
    uint32_t ring_idx = (log->count - 1 - offset) % log->capacity;
    return &log->entries[ring_idx];
}

uint32_t ctrl_log_visible_count(const ctrl_log_t *log) {
    if (!log) return 0;
    if (log->count < log->capacity) return log->count;
    return log->capacity;
}

void ctrl_log_scroll_up(ctrl_log_t *log, uint32_t lines) {
    if (!log) return;
    uint32_t avail = ctrl_log_visible_count(log);
    if (avail <= 1) return; /* Can't scroll with 0 or 1 entries. */

    log->scroll += lines;
    /* Clamp: can scroll up to (avail - 1) so index 0 still gets an entry. */
    if (log->scroll > avail - 1) {
        log->scroll = avail - 1;
    }
}

void ctrl_log_scroll_down(ctrl_log_t *log, uint32_t lines) {
    if (!log) return;
    if (lines >= log->scroll) {
        log->scroll = 0;
    } else {
        log->scroll -= lines;
    }
}

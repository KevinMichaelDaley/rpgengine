/**
 * @file ctrl_log.c
 * @brief Log ring buffer — lifecycle, add, query.
 */

#include "ferrum/editor/ctrl_log.h"
#include <stdlib.h>
#include <string.h>

bool ctrl_log_init(ctrl_log_t *log, uint32_t capacity) {
    if (!log) return false;
    if (capacity == 0) capacity = CTRL_LOG_DEFAULT_CAP;
    log->entries = (ctrl_log_entry_t *)calloc(capacity, sizeof(ctrl_log_entry_t));
    if (!log->entries) return false;
    log->capacity = capacity;
    log->count    = 0;
    log->scroll   = 0;
    return true;
}

void ctrl_log_destroy(ctrl_log_t *log) {
    if (!log) return;
    free(log->entries);
    log->entries  = NULL;
    log->capacity = 0;
    log->count    = 0;
}

void ctrl_log_add(ctrl_log_t *log, uint8_t level, const char *text) {
    if (!log || !log->entries || !text) return;
    uint32_t idx = log->count % log->capacity;
    ctrl_log_entry_t *e = &log->entries[idx];
    e->level = level;
    strncpy(e->text, text, CTRL_LOG_MAX_TEXT - 1);
    e->text[CTRL_LOG_MAX_TEXT - 1] = '\0';
    log->count++;
}

void ctrl_log_clear(ctrl_log_t *log) {
    if (!log) return;
    log->count  = 0;
    log->scroll = 0;
}

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
    e->level  = level;
    e->status = CTRL_LOG_STATUS_NONE;
    e->cmd_id = 0;
    strncpy(e->text, text, CTRL_LOG_MAX_TEXT - 1);
    e->text[CTRL_LOG_MAX_TEXT - 1] = '\0';
    log->count++;
}

void ctrl_log_add_cmd(ctrl_log_t *log, const char *text, uint32_t cmd_id) {
    if (!log || !log->entries || !text) return;
    uint32_t idx = log->count % log->capacity;
    ctrl_log_entry_t *e = &log->entries[idx];
    e->level  = 0;
    e->status = CTRL_LOG_STATUS_PENDING;
    e->cmd_id = cmd_id;
    strncpy(e->text, text, CTRL_LOG_MAX_TEXT - 1);
    e->text[CTRL_LOG_MAX_TEXT - 1] = '\0';
    log->count++;
}

bool ctrl_log_set_cmd_status(ctrl_log_t *log, uint32_t cmd_id,
                             ctrl_log_status_t status) {
    if (!log || !log->entries) return false;

    /* Search backwards from newest for matching cmd_id. */
    uint32_t search_limit = log->count < log->capacity
                                ? log->count : log->capacity;
    for (uint32_t i = 0; i < search_limit; i++) {
        uint32_t idx = (log->count - 1 - i) % log->capacity;
        ctrl_log_entry_t *e = &log->entries[idx];
        if (e->cmd_id == cmd_id && e->status == CTRL_LOG_STATUS_PENDING) {
            e->status = (uint8_t)status;
            return true;
        }
    }
    return false;
}

void ctrl_log_clear(ctrl_log_t *log) {
    if (!log) return;
    log->count  = 0;
    log->scroll = 0;
}

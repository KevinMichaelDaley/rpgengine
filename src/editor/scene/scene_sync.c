/**
 * @file scene_sync.c
 * @brief Sync lifecycle and queue operations — init, destroy, queue, dequeue.
 */

#include "ferrum/editor/scene/scene_sync.h"

#include <stdlib.h>
#include <string.h>

bool scene_sync_init(scene_sync_t *sync, const scene_sync_config_t *config) {
    if (!sync) return false;
    memset(sync, 0, sizeof(*sync));

    uint32_t cap = SCENE_SYNC_DEFAULT_QUEUE_CAP;
    if (config && config->queue_capacity > 0) {
        cap = config->queue_capacity;
    }

    sync->queue = (scene_sync_entry_t *)calloc(cap, sizeof(scene_sync_entry_t));
    if (!sync->queue) return false;

    sync->queue_capacity = cap;
    sync->queue_count    = 0;
    sync->queue_head     = 0;
    sync->queue_tail     = 0;
    sync->state          = SCENE_SYNC_IDLE;
    sync->in_flight      = 0;
    sync->force_save_pending = false;
    sync->initialized    = true;
    return true;
}

void scene_sync_destroy(scene_sync_t *sync) {
    if (!sync || !sync->initialized) return;
    free(sync->queue);
    sync->queue = NULL;
    sync->initialized = false;
}

bool scene_sync_queue_edit(scene_sync_t *sync, const char *cmd,
                           uint32_t cmd_id) {
    if (!sync || !sync->initialized || !cmd) return false;
    if (sync->queue_count >= sync->queue_capacity) return false;

    scene_sync_entry_t *entry = &sync->queue[sync->queue_tail];
    strncpy(entry->cmd, cmd, SCENE_SYNC_MAX_CMD_LEN - 1);
    entry->cmd[SCENE_SYNC_MAX_CMD_LEN - 1] = '\0';
    entry->cmd_id = cmd_id;

    sync->queue_tail = (sync->queue_tail + 1) % sync->queue_capacity;
    sync->queue_count++;
    return true;
}

bool scene_sync_dequeue_edit(scene_sync_t *sync, char *buf, size_t buf_cap,
                             uint32_t *cmd_id) {
    if (!sync || !sync->initialized || !buf || buf_cap == 0) return false;
    if (sync->queue_count == 0) return false;

    scene_sync_entry_t *entry = &sync->queue[sync->queue_head];
    strncpy(buf, entry->cmd, buf_cap - 1);
    buf[buf_cap - 1] = '\0';
    if (cmd_id) *cmd_id = entry->cmd_id;

    sync->queue_head = (sync->queue_head + 1) % sync->queue_capacity;
    sync->queue_count--;
    return true;
}

/**
 * @file scene_sync_state.c
 * @brief Sync state management, save force, and status formatting.
 */

#include "ferrum/editor/scene/scene_sync.h"

#include <stdio.h>

void scene_sync_mark_sent(scene_sync_t *sync) {
    if (!sync || !sync->initialized) return;
    sync->in_flight++;
}

void scene_sync_mark_acked(scene_sync_t *sync) {
    if (!sync || !sync->initialized) return;
    if (sync->in_flight > 0) {
        sync->in_flight--;
    }
}

void scene_sync_update_state(scene_sync_t *sync) {
    if (!sync || !sync->initialized) return;

    /* Offline state is sticky — only cleared externally on reconnect */
    if (sync->state == SCENE_SYNC_OFFLINE) return;

    if (sync->in_flight > 0) {
        sync->state = SCENE_SYNC_SYNCING;
    } else {
        sync->state = SCENE_SYNC_IDLE;
    }
}

void scene_sync_save_force(scene_sync_t *sync) {
    if (!sync || !sync->initialized) return;
    sync->force_save_pending = true;
}

bool scene_sync_consume_force_save(scene_sync_t *sync) {
    if (!sync || !sync->initialized) return false;
    if (sync->force_save_pending) {
        sync->force_save_pending = false;
        return true;
    }
    return false;
}

void scene_sync_format_status(const scene_sync_t *sync, char *buf, size_t cap) {
    if (!sync || !buf || cap == 0) return;

    switch (sync->state) {
        case SCENE_SYNC_IDLE:
            snprintf(buf, cap, "Synced");
            break;

        case SCENE_SYNC_SYNCING:
            snprintf(buf, cap, "Syncing... (%u)", sync->in_flight);
            break;

        case SCENE_SYNC_OFFLINE:
            if (sync->queue_count > 0) {
                snprintf(buf, cap, "Offline (%u queued)", sync->queue_count);
            } else {
                snprintf(buf, cap, "Offline");
            }
            break;
    }
}

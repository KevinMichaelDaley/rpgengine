/**
 * @file npc_nav_action.c
 * @brief GOTO navigation tool: target resolution + validation + async submit.
 *
 * Non-static functions (3 of 4 max):
 *   1. npc_nav_action_resolve_target
 *   2. npc_nav_action_register_landmark
 *   3. npc_nav_action_goto
 */

#include "ferrum/npc/npc_nav_action.h"
#include "ferrum/npc/npc_nav_world.h"
#include "ferrum/aegis/aegis_async.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define NPC_NAV_MAX_LANDMARKS 64
#define NPC_NAV_LANDMARK_NAME_MAX 64

typedef struct {
    char name[NPC_NAV_LANDMARK_NAME_MAX];
    phys_vec3_t pos;
} npc_nav_landmark_t;

static npc_nav_landmark_t g_nav_landmarks[NPC_NAV_MAX_LANDMARKS];
static int32_t g_nav_landmark_count = 0;

bool npc_nav_action_register_landmark(const char *name, phys_vec3_t pos) {
    if (!name || name[0] == '\0') return false;

    for (int32_t i = 0; i < g_nav_landmark_count; i++) {
        if (strcmp(g_nav_landmarks[i].name, name) == 0) {
            g_nav_landmarks[i].pos = pos;
            return true;
        }
    }

    if (g_nav_landmark_count >= NPC_NAV_MAX_LANDMARKS) return false;

    size_t name_len = strlen(name);
    size_t copy_len = name_len < NPC_NAV_LANDMARK_NAME_MAX
                          ? name_len
                          : NPC_NAV_LANDMARK_NAME_MAX - 1;
    memcpy(g_nav_landmarks[g_nav_landmark_count].name, name, copy_len);
    g_nav_landmarks[g_nav_landmark_count].name[copy_len] = '\0';
    g_nav_landmarks[g_nav_landmark_count].pos = pos;
    g_nav_landmark_count++;
    return true;
}

bool npc_nav_action_resolve_target(const npc_nav_world_t *nw,
                                   uint32_t actor_id,
                                   const char *target_name,
                                   phys_vec3_t *out_pos) {
    if (!target_name || !out_pos) return false;

    for (int32_t i = 0; i < g_nav_landmark_count; i++) {
        if (strcmp(g_nav_landmarks[i].name, target_name) == 0) {
            *out_pos = g_nav_landmarks[i].pos;
            return true;
        }
    }

    (void)nw;
    (void)actor_id;
    return false;
}

bool npc_nav_action_goto(npc_nav_world_t *nw,
                         uint32_t actor_id,
                         const void *world,
                         const char *target_name,
                         phys_vec3_t target_pos,
                         bool rooted,
                         bool stunned,
                         aegis_async_buffer_t *buf,
                         char *result_out,
                         uint32_t result_cap) {
    (void)world;
    (void)actor_id;
    if (!buf || !result_out || result_cap == 0) return false;

    result_out[0] = '\0';

    if (rooted || stunned) {
        snprintf(result_out, result_cap,
                 "GOTO failed: %s.",
                 rooted ? "rooted" : "stunned");
        return false;
    }

    /* If nw is absent, the target must have been pre-resolved.
     * A target_name of empty string means resolution failed. */
    if (!nw) {
        if (!target_name || target_name[0] == '\0') {
            snprintf(result_out, result_cap, "GOTO failed: unknown target.");
            return false;
        }
        /* Without nav world, cannot verify nav mesh. */
        snprintf(result_out, result_cap, "GOTO failed: nav system not available.");
        return false;
    }

    /* Submit nav query. */
    aegis_async_task_t task;
    memset(&task, 0, sizeof(task));
    atomic_store(&task.status, AEGIS_ASYNC_PENDING);
    task.task_type = AEGIS_TASK_NAV_QUERY;
    task.result_cap = 4096;

    float start[3] = {0, 0, 0};
    memcpy(task.params, start, 12);
    memcpy(task.params + 12, &target_pos.x, 12);
    uint32_t strat = 0;
    memcpy(task.params + 24, &strat, 4);
    float rad = 0.3f, h = 1.8f;
    memcpy(task.params + 28, &rad, 4);
    memcpy(task.params + 32, &h, 4);
    uint32_t max_wp = 64;
    memcpy(task.params + 36, &max_wp, 4);

    if (!aegis_async_buffer_submit(buf, &task)) {
        snprintf(result_out, result_cap,
                 "GOTO failed: async buffer full.");
        return false;
    }

    snprintf(result_out, result_cap,
             "Moving to %s.", target_name);
    return true;
}

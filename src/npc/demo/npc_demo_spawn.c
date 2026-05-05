/**
 * @file npc_demo_spawn.c
 * @brief NPC spawn helper: creates physics body and registers NPC state.
 *
 * Non-static functions (2 of 4 max):
 *   1. npc_demo_spawn_npc
 *   2. npc_demo_npc_list
 *
 * Static helpers:
 *   - spawn_find_free_slot_
 */

#include "ferrum/npc/npc_demo.h"
#include "ferrum/physics/phys_cmd.h"
#include "ferrum/physics/body.h"
#include "ferrum/net/topic_channel.h"
#include "ferrum/aegis/aegis_runtime.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static npc_demo_npc_t g_npcs[NPC_DEMO_MAX_NPCS];
static uint32_t       g_npc_count;

static int32_t spawn_find_free_slot_(void) {
    for (uint32_t i = 0; i < NPC_DEMO_MAX_NPCS; i++) {
        if (!g_npcs[i].active) return (int32_t)i;
    }
    return -1;
}

uint32_t npc_demo_spawn_npc(struct fr_topic_channel *cmd_channel,
                             const npc_demo_spawn_t *spawn) {
    if (!cmd_channel || !spawn) return UINT32_MAX;

    int32_t slot = spawn_find_free_slot_();
    if (slot < 0) return UINT32_MAX;

    phys_cmd_spawn_body_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.position    = (phys_vec3_t){spawn->position[0],
                                    spawn->position[1],
                                    spawn->position[2]};
    cmd.orientation = (phys_quat_t){0.0f, 0.0f, 0.0f, 1.0f};
    cmd.mass        = 75.0f;
    cmd.flags       = 0;
    cmd.shape       = PHYS_CMD_SHAPE_CAPSULE;
    cmd.shape_data.capsule.radius      = 0.3f;
    cmd.shape_data.capsule.half_height = 0.9f;
    cmd.user_tag    = (uint64_t)slot;

    if (!phys_cmd_push(cmd_channel, PHYS_CMD_SPAWN_BODY,
                       &cmd, sizeof(cmd))) {
        return UINT32_MAX;
    }

    g_npcs[slot].config      = *spawn;
    g_npcs[slot].body_index  = UINT32_MAX;
    g_npcs[slot].script_id   = AEGIS_SCRIPT_ID_INVALID;
    g_npcs[slot].active      = true;
    g_npcs[slot].tick_count  = 0;
    g_npcs[slot].last_sense_us = 0;
    g_npcs[slot].last_llm_us   = 0;
    g_npcs[slot].last_nav_us   = 0;

    if (slot >= (int32_t)g_npc_count) {
        g_npc_count = (uint32_t)(slot + 1);
    }

    return spawn->entity_id;
}

const npc_demo_npc_t *npc_demo_npc_list(uint32_t *out_count) {
    if (out_count) *out_count = g_npc_count;
    return g_npcs;
}

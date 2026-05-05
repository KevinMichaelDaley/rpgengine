/**
 * @file npc_demo.h
 * @brief NPC demo spawn and tick helpers for demo_server integration.
 */

#ifndef FERRUM_NPC_DEMO_H
#define FERRUM_NPC_DEMO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define NPC_DEMO_MAX_NPCS    64
#define NPC_DEMO_NAME_MAX    48
#define NPC_DEMO_STATBLOCK_SIZE 4

typedef struct npc_demo_spawn {
    uint32_t entity_id;
    float    position[3];
    char     name[NPC_DEMO_NAME_MAX];
    uint32_t statblock[NPC_DEMO_STATBLOCK_SIZE];
} npc_demo_spawn_t;

typedef struct npc_demo_npc {
    npc_demo_spawn_t config;
    uint32_t         body_index;
    uint32_t         script_id;
    bool             active;
    uint32_t         tick_count;
    uint64_t         last_sense_us;
    uint64_t         last_llm_us;
    uint64_t         last_nav_us;
} npc_demo_npc_t;

struct fr_topic_channel;
struct aegis_script_runtime;
struct aegis_async_buffer;
struct phys_world;

/* ── Non-static functions (npc_demo_spawn.c) ───────────────────── */

uint32_t npc_demo_spawn_npc(struct fr_topic_channel *cmd_channel,
                             const npc_demo_spawn_t *spawn);

const npc_demo_npc_t *npc_demo_npc_list(uint32_t *out_count);

/* ── Non-static functions (npc_demo_tick.c) ─────────────────────── */

void npc_demo_tick(struct aegis_script_runtime *script_rt,
                   struct aegis_async_buffer *async_buf,
                   const struct phys_world *world);

uint32_t npc_demo_tick_run_one(npc_demo_npc_t *npc,
                                struct aegis_script_runtime *script_rt,
                                struct aegis_async_buffer *async_buf,
                                const struct phys_world *world);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_NPC_DEMO_H */

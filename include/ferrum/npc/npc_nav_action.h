/**
 * @file npc_nav_action.h
 * @brief GOTO navigation tool: target resolution, validation, async submit.
 */

#ifndef FERRUM_NPC_NAV_ACTION_H
#define FERRUM_NPC_NAV_ACTION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "ferrum/physics/phys_vec3.h"

struct aegis_async_buffer;
struct npc_nav_world;

typedef struct npc_nav_action_result {
    int32_t status;
    char    message[];
} npc_nav_action_result_t;

/**
 * @brief Resolve a named target to a world position.
 *
 * Checks named entities, then the static landmark table.
 * @return true if resolved.
 */
bool npc_nav_action_resolve_target(const struct npc_nav_world *nw,
                                   uint32_t actor_id,
                                   const char *target_name,
                                   phys_vec3_t *out_pos);

/**
 * @brief Execute the GOTO tool action.
 *
 * @param nw          Nav world (may be NULL).
 * @param actor_id    Entity ID of the moving NPC.
 * @param world       Physics world pointer (reserved, may be NULL).
 * @param target_name Target entity/landmark name.
 * @param target_pos  Pre-resolved target position (if known).
 * @param rooted      Actor is rooted (cannot move).
 * @param stunned     Actor is stunned (cannot act).
 * @param buf         Async buffer to submit nav query.
 * @param result_out  Output buffer for result text.
 * @param result_cap  Capacity of result_out.
 * @return true on success, false on failure (error text in result_out).
 */
/**
 * @brief Register a landmark name → position mapping in the static table.
 *
 * @param name  Landmark name (copied, truncated to internal max).
 * @param pos   World position.
 * @return true on success, false if table is full or name is NULL/empty.
 */
bool npc_nav_action_register_landmark(const char *name, phys_vec3_t pos);

bool npc_nav_action_goto(struct npc_nav_world *nw,
                         uint32_t actor_id,
                         const void *world,
                         const char *target_name,
                         phys_vec3_t target_pos,
                         bool rooted,
                         bool stunned,
                         struct aegis_async_buffer *buf,
                         char *result_out,
                         uint32_t result_cap);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NPC_NAV_ACTION_H */

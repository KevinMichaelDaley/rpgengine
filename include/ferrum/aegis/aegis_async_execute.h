#ifndef FERRUM_AEGIS_ASYNC_EXECUTE_H
#define FERRUM_AEGIS_ASYNC_EXECUTE_H

/** @file
 * @brief Async task executor: drains the MPSC buffer and executes tasks.
 *
 * Currently handles VIS_TEST tasks by performing batched raycasts against
 * the physics world (both spatial grid broadphase and static BVH).
 * NAV_QUERY tasks are stubbed as ERROR until pathfinding is implemented.
 *
 * Result packing for VIS_TEST (16 bytes):
 *   - bytes  0..3:  distance (float, -1.0f on miss)
 *   - bytes  4..15: hit_point (float[3], zeroed on miss)
 *
 * Ownership: borrows buffer and world; does not allocate heap memory.
 * Nullability: NULL buffer or world returns 0.
 */

#include <stdint.h>

struct aegis_async_buffer;
struct phys_world;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Drain pending tasks from @p buf and execute them.
 *
 * For VIS_TEST tasks: extracts origin + ray_vec from params, normalizes
 * direction, computes max_distance from magnitude, calls phys_raycast(),
 * and writes the packed result to result_ptr.
 *
 * For NAV_QUERY tasks: writes error sentinel (distance = -1.0f) and
 * sets status to ERROR (not yet implemented).
 *
 * @param buf        MPSC async buffer to drain (NULL-safe, returns 0).
 * @param world      Physics world for raycast queries (NULL-safe, returns 0).
 * @param max_tasks  Maximum number of tasks to drain and execute this call.
 * @return Number of tasks executed.
 *
 * Side effects: writes to each task's result_ptr, modifies task status.
 */
uint32_t aegis_async_execute_drain(struct aegis_async_buffer *buf,
                                   const struct phys_world *world,
                                   uint32_t max_tasks);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_AEGIS_ASYNC_EXECUTE_H */

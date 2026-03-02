/**
 * @file aegis_async_execute.c
 * @brief Drains async tasks and dispatches raycasts against the physics world.
 *
 * Handles VIS_TEST by converting params → phys_ray_t, calling phys_raycast(),
 * and packing the result into the task's result_ptr.
 *
 * NAV_QUERY tasks are set to ERROR (not yet implemented).
 *
 * Non-static functions (2 of 4 max):
 *   1. aegis_async_execute_drain
 *
 * Static helpers:
 *   - execute_vis_test_   — single VIS_TEST → phys_raycast
 *   - write_miss_result_  — write miss sentinel to result_ptr
 */

#include "ferrum/aegis/aegis_async_execute.h"

#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/aegis/aegis_async.h"
#include "ferrum/physics/raycast.h"
#include "ferrum/physics/world.h"

/* ── Miss sentinel ─────────────────────────────────────────────── */

/**
 * @brief Write miss result: distance = -1.0f, hit_point = (0,0,0).
 */
static void write_miss_result_(void *result_ptr) {
    if (!result_ptr) return;
    float miss[4] = {-1.0f, 0.0f, 0.0f, 0.0f};
    memcpy(result_ptr, miss, 16);
}

/* ── VIS_TEST executor ─────────────────────────────────────────── */

/**
 * @brief Execute a single VIS_TEST task as a raycast.
 *
 * Param layout (24 bytes):
 *   params[ 0..11] = origin   (float[3])
 *   params[12..23] = ray_vec  (float[3])
 *
 * ray_vec encodes direction (normalized) and max_distance (magnitude).
 * Result packing (16 bytes):
 *   result[ 0.. 3] = distance (float, -1.0f on miss)
 *   result[ 4..15] = hit_point (float[3], zeroed on miss)
 */
static void execute_vis_test_(const aegis_async_task_t *task,
                              const struct phys_world *world) {
    if (!task->result_ptr) return;

    /* Extract origin and ray_vec from params. */
    float origin[3];
    float ray_vec[3];
    memcpy(origin, task->params, 12);
    memcpy(ray_vec, task->params + 12, 12);

    /* Compute direction and max_distance from ray_vec magnitude. */
    float mag = sqrtf(ray_vec[0] * ray_vec[0] +
                      ray_vec[1] * ray_vec[1] +
                      ray_vec[2] * ray_vec[2]);
    if (mag < 1e-9f) {
        write_miss_result_(task->result_ptr);
        return;
    }

    phys_ray_t ray;
    ray.origin.x    = origin[0];
    ray.origin.y    = origin[1];
    ray.origin.z    = origin[2];
    ray.direction.x = ray_vec[0] / mag;
    ray.direction.y = ray_vec[1] / mag;
    ray.direction.z = ray_vec[2] / mag;
    ray.max_distance = mag;

    phys_raycast_hit_t hit;
    memset(&hit, 0, sizeof(hit));

    /* All layers. */
    bool did_hit = phys_raycast(world, &ray, &hit, 0xFFFFFFFFu);

    if (did_hit) {
        /* Pack: distance + hit_point. */
        float result[4];
        result[0] = hit.distance;
        result[1] = hit.point.x;
        result[2] = hit.point.y;
        result[3] = hit.point.z;
        memcpy(task->result_ptr, result, 16);
    } else {
        write_miss_result_(task->result_ptr);
    }
}

/* ── Public API ────────────────────────────────────────────────── */

uint32_t aegis_async_execute_drain(struct aegis_async_buffer *buf,
                                   const struct phys_world *world,
                                   uint32_t max_tasks) {
    if (!buf || !world || max_tasks == 0) return 0;

    /* Drain up to max_tasks. */
    aegis_async_task_t *tasks = (aegis_async_task_t *)malloc(
        max_tasks * sizeof(aegis_async_task_t));
    if (!tasks) return 0;

    uint32_t drained = aegis_async_buffer_drain(buf, tasks, max_tasks);

    for (uint32_t i = 0; i < drained; i++) {
        aegis_async_task_t *t = &tasks[i];
        uint32_t final_status = AEGIS_ASYNC_COMPLETE;

        switch (t->task_type) {
        case AEGIS_TASK_VIS_TEST:
            execute_vis_test_(t, world);
            break;

        case AEGIS_TASK_NAV_QUERY:
            /* Not yet implemented — write miss/error sentinel. */
            write_miss_result_(t->result_ptr);
            final_status = AEGIS_ASYNC_ERROR;
            final_status = AEGIS_ASYNC_ERROR;
            break;

        default:
            write_miss_result_(t->result_ptr);
            final_status = AEGIS_ASYNC_ERROR;
            break;
        }

        /* Write back completion status to the VM's tracking entry.
         * Use release ordering so result writes are visible before
         * the VM's poll/wait reads the status with acquire ordering. */
        if (t->status_ptr) {
            atomic_store_explicit(t->status_ptr, final_status,
                                 memory_order_release);
        }
    }

    free(tasks);
    return drained;
}

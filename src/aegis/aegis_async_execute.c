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
#include "ferrum/aegis/aegis_sense.h"
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

/* ── SENSE_QUERY executor ──────────────────────────────────────── */

/**
 * @brief Internal candidate record during sense sweep.
 */
typedef struct sense_candidate {
    uint32_t entity_id;
    float    distance;
    float    salience;
    uint16_t flags;
} sense_candidate_t;

/**
 * @brief Compute Euclidean distance between two 3D points.
 */
static float dist3_(const float a[3], const float b[3]) {
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

/**
 * @brief Execute a single SENSE_QUERY task.
 *
 * Param layout (24 bytes):
 *   params[0..1]   = query_mode    (uint16_t)
 *   params[2..3]   = sense_flags   (uint16_t)
 *   params[4..7]   = target_entity (uint32_t)
 *   params[8..19]  = npc_position  (float[3])
 *   params[20..23] = max_range     (float)
 *
 * Result layout (variable, up to result_cap):
 *   aegis_sense_result_t header
 *   entity_count × aegis_sense_entity_t
 *   event_count  × aegis_sense_event_t
 */
static void execute_sense_query_(const aegis_async_task_t *task,
                                 const struct phys_world *world) {
    if (!task->result_ptr || task->result_cap < sizeof(aegis_sense_result_t)) {
        return;
    }

    /* Unpack params. */
    uint16_t query_mode;
    uint16_t sense_flags;
    uint32_t target_entity;
    float    npc_pos[3];
    float    max_range;
    memcpy(&query_mode,     task->params,      2);
    memcpy(&sense_flags,    task->params + 2,  2);
    memcpy(&target_entity,  task->params + 4,  4);
    memcpy(npc_pos,         task->params + 8,  12);
    memcpy(&max_range,      task->params + 20, 4);

    /* Write header. */
    aegis_sense_result_t *header = (aegis_sense_result_t *)task->result_ptr;
    memset(header, 0, sizeof(*header));
    header->status = 0;

    /* Gather candidates from physics world. */
    sense_candidate_t candidates[128];
    uint32_t candidate_count = 0;
    uint32_t max_candidates = sizeof(candidates) / sizeof(candidates[0]);

    const phys_body_pool_t *pool = &world->body_pool;
    uint32_t body_count = pool->count;
    const phys_body_t *bodies = pool->bodies_curr;
    const uint8_t *active = pool->active;

    for (uint32_t bi = 0; bi < body_count && candidate_count < max_candidates; bi++) {
        if (!active[bi]) continue;
        const phys_body_t *b = &bodies[bi];
        uint32_t eid = b->entity_index;
        if (eid == UINT32_MAX) continue;

        float d = dist3_(npc_pos, (const float *)&b->position);
        if (d > max_range) continue;

        /* In targeted mode, only accept the target entity. */
        if (query_mode == AEGIS_SENSE_MODE_TARGETED && eid != target_entity) {
            continue;
        }

        uint16_t flags = 0;
        if (sense_flags & AEGIS_SENSE_PROXIMITY) {
            flags |= AEGIS_SENSE_ENTITY_VISIBLE; /* proximity = "detected nearby" */
        }

        /* LOS raycast (expensive — only for near-range candidates). */
        if ((sense_flags & AEGIS_SENSE_LOS) && d < max_range) {
            phys_ray_t ray;
            ray.origin.x    = npc_pos[0];
            ray.origin.y    = npc_pos[1];
            ray.origin.z    = npc_pos[2];
            ray.direction.x = (b->position.x - npc_pos[0]) / d;
            ray.direction.y = (b->position.y - npc_pos[1]) / d;
            ray.direction.z = (b->position.z - npc_pos[2]) / d;
            ray.max_distance = d + 0.5f; /* slight margin */

            phys_raycast_hit_t hit;
            memset(&hit, 0, sizeof(hit));
            if (phys_raycast(world, &ray, &hit, 0xFFFFFFFFu)) {
                /* Hit something. If it's our target body, LOS is clear. */
                if (hit.body_id == bi) {
                    flags |= AEGIS_SENSE_ENTITY_VISIBLE;
                }
            }
        }

        /* Audio, smell, shadow are distance-approximated (stubs). */
        if ((sense_flags & AEGIS_SENSE_AUDIO) && d < max_range * 0.5f) {
            flags |= AEGIS_SENSE_ENTITY_AUDIBLE;
        }
        if ((sense_flags & AEGIS_SENSE_SMELL) && d < max_range * 0.3f) {
            flags |= AEGIS_SENSE_ENTITY_SMELLED;
        }

        float salience = 1.0f - (d / max_range);
        if (salience < 0.0f) salience = 0.0f;
        if (salience > 1.0f) salience = 1.0f;

        candidates[candidate_count].entity_id = eid;
        candidates[candidate_count].distance  = d;
        candidates[candidate_count].salience  = salience;
        candidates[candidate_count].flags     = flags;
        candidate_count++;
    }

    /* Determine how many entities fit in the result buffer. */
    uint8_t *write_ptr = (uint8_t *)task->result_ptr + sizeof(aegis_sense_result_t);
    uint32_t remaining = task->result_cap - sizeof(aegis_sense_result_t);
    uint32_t written = 0;

    for (uint32_t i = 0; i < candidate_count; i++) {
        uint32_t need = sizeof(aegis_sense_entity_t); /* conservative: no name */
        if (need > remaining) {
            header->truncated = 1;
            break;
        }
        aegis_sense_entity_t *ent = (aegis_sense_entity_t *)write_ptr;
        ent->entity_id = candidates[i].entity_id;
        ent->distance  = candidates[i].distance;
        ent->salience  = candidates[i].salience;
        ent->flags     = candidates[i].flags;
        ent->name[0]   = '\0';
        write_ptr += need;
        remaining -= need;
        written++;
    }

    header->entity_count = written;
    header->total_found  = candidate_count;
    header->event_count  = 0;
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

        case AEGIS_TASK_SENSE_QUERY:
            execute_sense_query_(t, world);
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

/**
 * @file prediction_tick.c
 * @brief Lightweight client-side prediction integrator.
 *
 * Runs integration on a dedicated thread at a fixed timestep,
 * decoupled from the render frame rate.  Matches the server physics
 * tick rate so client prediction stays in sync.  The render thread
 * reads bodies_curr; the prediction thread writes bodies_next then
 * swaps — double buffering keeps them from colliding.
 *
 * Non-static functions (4): create, destroy, start, stop.
 */

/* Need _POSIX_C_SOURCE >= 200112L for clock_nanosleep. */
#define _POSIX_C_SOURCE 200112L

#include "ferrum/net/replication/prediction_tick.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/body.h"
#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/** Velocity damping retained per second (matches server default). */
#define VEL_DAMPING 0.999f

/** Max linear speed (m/s) — clamp to prevent runaway. */
#define MAX_LIN_SPEED 100.0f

/** Max angular speed (rad/s). */
#define MAX_ANG_SPEED 50.0f

struct fr_prediction_tick {
    float    fixed_dt;
    vec3_t   gravity;
    uint32_t max_bodies;

    /* Thread state. */
    pthread_t        thread;
    atomic_int       running;  /**< 1 while thread should keep ticking. */
    phys_body_pool_t *pool;    /**< Borrowed — must outlive the thread. */
};

/* ── create / destroy ───────────────────────────────────────── */

fr_prediction_tick_t *fr_prediction_tick_create(
    const fr_prediction_tick_config_t *cfg)
{
    if (!cfg || cfg->fixed_dt <= 0.0f || cfg->max_bodies == 0) return NULL;

    fr_prediction_tick_t *pt = calloc(1, sizeof(*pt));
    if (!pt) return NULL;

    pt->fixed_dt   = cfg->fixed_dt;
    pt->gravity    = cfg->gravity;
    pt->max_bodies = cfg->max_bodies;
    atomic_store(&pt->running, 0);
    return pt;
}

void fr_prediction_tick_destroy(fr_prediction_tick_t *pt)
{
    if (!pt) return;
    fr_prediction_tick_stop(pt);
    free(pt);
}

/* ── single integration step ────────────────────────────────── */

/**
 * Integrate all active dynamic bodies one fixed timestep.
 * Reads from bodies_curr, writes to bodies_next, then swaps.
 */
static void integrate_step_(phys_body_pool_t *pool,
                            vec3_t gravity,
                            float dt)
{
    const uint32_t cap = pool->capacity;
    const float half_dt = 0.5f * dt;
    const float damp = powf(VEL_DAMPING, dt);

    for (uint32_t i = 0; i < cap; i++) {
        if (!pool->active[i]) continue;

        const phys_body_t *in  = &pool->bodies_curr[i];
        phys_body_t       *out = &pool->bodies_next[i];

        /* Copy all fields first (flags, tier, mass, collider, etc). */
        *out = *in;

        /* Static / sleeping bodies: no integration. */
        if (in->inv_mass <= 0.0f) continue;
        if (in->flags & PHYS_BODY_FLAG_SLEEPING) continue;

        /* Apply gravity to linear velocity. */
        out->linear_vel = vec3_add(in->linear_vel,
                                   vec3_scale(gravity, dt));

        /* Velocity damping. */
        out->linear_vel  = vec3_scale(out->linear_vel, damp);
        out->angular_vel = vec3_scale(in->angular_vel, damp);

        /* Clamp velocities. */
        {
            float ls = vec3_magnitude(out->linear_vel);
            if (ls > MAX_LIN_SPEED) {
                out->linear_vel = vec3_scale(out->linear_vel,
                                             MAX_LIN_SPEED / ls);
            }
            float as = vec3_magnitude(out->angular_vel);
            if (as > MAX_ANG_SPEED) {
                out->angular_vel = vec3_scale(out->angular_vel,
                                              MAX_ANG_SPEED / as);
            }
        }

        /* Integrate position: pos += vel * dt. */
        out->position = vec3_add(in->position,
                                 vec3_scale(out->linear_vel, dt));

        /* Integrate orientation via quaternion derivative:
         *   omega_q = {ang.x, ang.y, ang.z, 0}
         *   dq = quat_mul(omega_q, orientation)
         *   orientation += 0.5 * dq * dt
         * Matches server integrate.c exactly. */
        phys_quat_t omega_q = {
            out->angular_vel.x,
            out->angular_vel.y,
            out->angular_vel.z,
            0.0f
        };
        phys_quat_t dq = quat_mul(omega_q, in->orientation);
        out->orientation.x = in->orientation.x + dq.x * half_dt;
        out->orientation.y = in->orientation.y + dq.y * half_dt;
        out->orientation.z = in->orientation.z + dq.z * half_dt;
        out->orientation.w = in->orientation.w + dq.w * half_dt;
        out->orientation = quat_normalize_safe(out->orientation, 1e-8f);
    }

    phys_body_pool_swap_buffers(pool);
}

/* ── thread entry point ─────────────────────────────────────── */

static void *prediction_thread_(void *arg)
{
    fr_prediction_tick_t *pt = (fr_prediction_tick_t *)arg;
    const long tick_ns = (long)(pt->fixed_dt * 1e9f);

    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);

    while (atomic_load(&pt->running)) {
        integrate_step_(pt->pool, pt->gravity, pt->fixed_dt);

        /* Advance target time by one fixed tick. */
        next.tv_nsec += tick_ns;
        if (next.tv_nsec >= 1000000000L) {
            next.tv_sec  += next.tv_nsec / 1000000000L;
            next.tv_nsec  = next.tv_nsec % 1000000000L;
        }

        /* Sleep until next tick (absorbs jitter). */
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }
    return NULL;
}

/* ── start / stop ───────────────────────────────────────────── */

bool fr_prediction_tick_start(fr_prediction_tick_t *pt,
                              phys_body_pool_t *pool)
{
    if (!pt || !pool) return false;
    if (atomic_load(&pt->running)) return false; /* already running */

    pt->pool = pool;
    atomic_store(&pt->running, 1);

    if (pthread_create(&pt->thread, NULL, prediction_thread_, pt) != 0) {
        atomic_store(&pt->running, 0);
        return false;
    }
    return true;
}

void fr_prediction_tick_stop(fr_prediction_tick_t *pt)
{
    if (!pt) return;
    if (!atomic_load(&pt->running)) return;

    atomic_store(&pt->running, 0);
    pthread_join(pt->thread, NULL);
}

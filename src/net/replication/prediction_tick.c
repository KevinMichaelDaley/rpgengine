/**
 * @file prediction_tick.c
 * @brief Lightweight client-side prediction integrator with triple-buffer
 *        reconciliation.
 *
 * Runs integration on a dedicated thread at a fixed timestep,
 * decoupled from the render frame rate.  Each tick:
 *   1. For each body, copy curr → next.
 *   2. If bodies_net has new server data (atomic dirty flag), reconcile
 *      next toward the server-authoritative state (snap or blend).
 *   3. Integrate next forward (gravity, velocity, orientation).
 *   4. Swap curr ↔ next.
 *
 * The render thread reads bodies_curr lock-free.  The recv thread
 * writes bodies_net lock-free.  Only the prediction thread touches
 * bodies_next and performs the swap.
 *
 * Non-static functions (4): create, destroy, start, stop.
 */

/* Need _POSIX_C_SOURCE >= 200112L for clock_nanosleep. */
#define _POSIX_C_SOURCE 200112L

#include "ferrum/net/replication/prediction_tick.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/snapshot.h"
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

/** Small epsilon below which position error is considered negligible. */
#define RECONCILE_EPSILON 0.001f

struct fr_prediction_tick {
    float    fixed_dt;
    vec3_t   gravity;
    uint32_t max_bodies;
    phys_prediction_config_t reconcile; /**< Snap/blend thresholds. */

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
    pt->reconcile  = cfg->reconcile;
    atomic_store(&pt->running, 0);
    return pt;
}

void fr_prediction_tick_destroy(fr_prediction_tick_t *pt)
{
    if (!pt) return;
    fr_prediction_tick_stop(pt);
    free(pt);
}

/** Quantization scale for position (mm precision, matches snapshot). */
#define POS_QUANT_SCALE 1000.0f

/* ── per-body reconciliation against server authority ───────── */

/**
 * Check whether two positions are identical at the wire quantization
 * level (int16 mm).  If so, any float difference is pure roundtrip
 * noise and should not trigger reconciliation.
 */
static bool positions_match_quantized_(const phys_vec3_t *a,
                                       const phys_vec3_t *b)
{
    int16_t qa[3], qb[3];
    phys_quantize_vec3(*a, qa, POS_QUANT_SCALE);
    phys_quantize_vec3(*b, qb, POS_QUANT_SCALE);
    return qa[0] == qb[0] && qa[1] == qb[1] && qa[2] == qb[2];
}

/**
 * Reconcile a single body toward server-authoritative state.
 * Reads net body (server truth), writes corrections into out (bodies_next).
 *
 * If the server reports the body as sleeping, snap to exact server state
 * and preserve the sleeping flag — no blend, no integration will follow.
 *
 * If the local and server positions are identical at the quantization
 * level (int16 mm), skip reconciliation — the difference is pure
 * roundtrip noise and blending toward it causes visible jitter.
 *
 * Otherwise snap (large errors) or blend (small errors) toward server.
 */
static void reconcile_body_(phys_body_t *out,
                            const phys_body_t *net,
                            const phys_prediction_config_t *cfg)
{
    /* Server says sleeping: accept server pose exactly, mark sleeping.
     * No blend — quantization roundtrip noise would cause jitter. */
    if (net->flags & PHYS_BODY_FLAG_SLEEPING) {
        out->position    = net->position;
        out->orientation = net->orientation;
        out->linear_vel  = (phys_vec3_t){0, 0, 0};
        out->angular_vel = (phys_vec3_t){0, 0, 0};
        out->flags      |= PHYS_BODY_FLAG_SLEEPING;
        return;
    }

    /* Server says awake: clear sleeping flag so integration runs. */
    out->flags &= ~(uint32_t)PHYS_BODY_FLAG_SLEEPING;

    /* Propagate contact-resting flag from server authority.
     * This tells the client whether gravity is countered by collision. */
    if (net->flags & PHYS_BODY_FLAG_CONTACT_RESTING) {
        out->flags |= PHYS_BODY_FLAG_CONTACT_RESTING;
    } else {
        out->flags &= ~(uint32_t)PHYS_BODY_FLAG_CONTACT_RESTING;
    }

    /* If positions are identical at wire quantization, the float
     * difference is pure roundtrip noise — skip reconciliation. */
    if (positions_match_quantized_(&out->position, &net->position)) {
        return;
    }
    /* Position error (Euclidean distance). */
    vec3_t diff = vec3_sub(
        (vec3_t){out->position.x, out->position.y, out->position.z},
        (vec3_t){net->position.x, net->position.y, net->position.z});
    float pos_err = vec3_magnitude(diff);

    /* Rotation error: angle = 2 * acos(|dot(q1, q2)|). */
    float dot = out->orientation.x * net->orientation.x
              + out->orientation.y * net->orientation.y
              + out->orientation.z * net->orientation.z
              + out->orientation.w * net->orientation.w;
    float abs_dot = fabsf(dot);
    if (abs_dot > 1.0f) abs_dot = 1.0f;
    float rot_err = 2.0f * acosf(abs_dot);

    if (pos_err > cfg->position_snap_threshold ||
        rot_err > cfg->rotation_snap_threshold) {
        /* Snap: teleport to server state. */
        out->position    = net->position;
        out->orientation = net->orientation;
        out->linear_vel  = net->linear_vel;
        out->angular_vel = net->angular_vel;
    } else if (pos_err > RECONCILE_EPSILON) {
        /* Blend position (lerp) and orientation (slerp). */
        float pr = cfg->position_blend_rate;
        out->position.x += (net->position.x - out->position.x) * pr;
        out->position.y += (net->position.y - out->position.y) * pr;
        out->position.z += (net->position.z - out->position.z) * pr;

        quat_t q_out = {out->orientation.x, out->orientation.y,
                        out->orientation.z, out->orientation.w};
        quat_t q_net = {net->orientation.x, net->orientation.y,
                        net->orientation.z, net->orientation.w};
        quat_t q_blend = quat_slerp(q_out, q_net,
                                     cfg->rotation_blend_rate, 1e-6f);
        out->orientation.x = q_blend.x;
        out->orientation.y = q_blend.y;
        out->orientation.z = q_blend.z;
        out->orientation.w = q_blend.w;

        /* Blend velocities to reduce drift. */
        float vr = 0.3f;
        out->linear_vel.x += (net->linear_vel.x - out->linear_vel.x) * vr;
        out->linear_vel.y += (net->linear_vel.y - out->linear_vel.y) * vr;
        out->linear_vel.z += (net->linear_vel.z - out->linear_vel.z) * vr;
        out->angular_vel.x += (net->angular_vel.x - out->angular_vel.x) * vr;
        out->angular_vel.y += (net->angular_vel.y - out->angular_vel.y) * vr;
        out->angular_vel.z += (net->angular_vel.z - out->angular_vel.z) * vr;
    }
}

/* ── single integration step (3-way merge) ──────────────────── */

/**
 * For each active body:
 *   1. Copy curr → next  (all fields).
 *   2. If net_dirty, reconcile next toward bodies_net (server truth).
 *   3. Integrate next forward one timestep.
 *   4. Swap curr ↔ next.
 */
static void integrate_step_(phys_body_pool_t *pool,
                            vec3_t gravity,
                            float dt,
                            const phys_prediction_config_t *reconcile)
{
    const uint32_t cap = pool->capacity;
    const float half_dt = 0.5f * dt;
    const float damp = powf(VEL_DAMPING, dt);

    for (uint32_t i = 0; i < cap; i++) {
        if (!pool->active[i]) continue;

        const phys_body_t *in  = &pool->bodies_curr[i];
        phys_body_t       *out = &pool->bodies_next[i];

        /* 1. Copy all fields from curr to next. */
        *out = *in;

        /* 2. Reconcile from server authority if new data arrived. */
        if (phys_body_pool_consume_net_dirty(pool, i)) {
            reconcile_body_(out, &pool->bodies_net[i], reconcile);
        }

        /* Static / sleeping bodies: no integration. */
        if (out->inv_mass <= 0.0f) continue;
        if (out->flags & PHYS_BODY_FLAG_SLEEPING) continue;

        /* Server flagged this body as contact-supported (gravity
         * countered by collision).  Without client-side collision we
         * cannot keep it on the surface, so skip integration entirely
         * to avoid gravity pulling it through the floor. */
        if (out->flags & PHYS_BODY_FLAG_CONTACT_RESTING) continue;

        /* 3. Integrate from the (possibly reconciled) state in out. */

        /* Apply gravity to linear velocity. */
        out->linear_vel = vec3_add(out->linear_vel,
                                   vec3_scale(gravity, dt));

        /* Velocity damping. */
        out->linear_vel  = vec3_scale(out->linear_vel, damp);
        out->angular_vel = vec3_scale(out->angular_vel, damp);

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
        out->position = vec3_add(out->position,
                                 vec3_scale(out->linear_vel, dt));

        /* Integrate orientation via quaternion derivative. */
        phys_quat_t omega_q = {
            out->angular_vel.x,
            out->angular_vel.y,
            out->angular_vel.z,
            0.0f
        };
        phys_quat_t dq = quat_mul(omega_q, out->orientation);
        out->orientation.x += dq.x * half_dt;
        out->orientation.y += dq.y * half_dt;
        out->orientation.z += dq.z * half_dt;
        out->orientation.w += dq.w * half_dt;
        out->orientation = quat_normalize_safe(out->orientation, 1e-8f);
    }

    /* 4. Swap: next becomes curr for render + next prediction tick. */
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
        integrate_step_(pt->pool, pt->gravity, pt->fixed_dt,
                        &pt->reconcile);

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

/**
 * @file priority_body_sender.c
 * @brief Velocity-proportional priority BODY_STATE sender.
 *
 * Non-static functions (3): create, destroy, tick.
 */

#include "ferrum/server/physics/net/priority_body_sender.h"
#include "ferrum/net/replication/body_state.h"
#include "ferrum/net/replication/common.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/world.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct fr_priority_body_sender {
    uint32_t max_bodies;
    float    speed_full_rate;
    float    speed_min;
    uint32_t max_interval;

    /** Per-body: tick number when this body last sent an update. */
    uint64_t *last_send_tick;

    /** Per-body scratch: computed send interval for current tick.
     *  Used to propagate min interval across joint partners. */
    uint32_t *intervals;
};

/* ── create / destroy ───────────────────────────────────────── */

fr_priority_body_sender_t *fr_priority_body_sender_create(
    const fr_priority_body_sender_config_t *cfg)
{
    if (!cfg || cfg->max_bodies == 0) return NULL;

    fr_priority_body_sender_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;

    s->last_send_tick = calloc(cfg->max_bodies, sizeof(uint64_t));
    if (!s->last_send_tick) {
        free(s);
        return NULL;
    }

    s->intervals = calloc(cfg->max_bodies, sizeof(uint32_t));
    if (!s->intervals) {
        free(s->last_send_tick);
        free(s);
        return NULL;
    }

    s->max_bodies     = cfg->max_bodies;
    s->speed_full_rate = (cfg->speed_full_rate > 0.0f)
                         ? cfg->speed_full_rate : 10.0f;
    s->speed_min       = (cfg->speed_min > 0.0f)
                         ? cfg->speed_min : 0.3f;
    s->max_interval    = (cfg->max_interval > 0)
                         ? cfg->max_interval : 30;
    return s;
}

void fr_priority_body_sender_destroy(fr_priority_body_sender_t *s)
{
    if (!s) return;
    free(s->intervals);
    free(s->last_send_tick);
    free(s);
}

/* ── Compute interval from speed ────────────────────────────── */

/**
 * Map speed to a send interval in physics ticks.
 *
 * speed <= speed_min  → UINT32_MAX (skip entirely)
 * speed >= speed_full → 1 (every tick)
 * in between          → lerp from max_interval down to 1
 */
static uint32_t interval_from_speed_(float speed,
                                     float speed_min,
                                     float speed_full,
                                     uint32_t max_interval)
{
    if (speed <= speed_min) return UINT32_MAX;
    if (speed >= speed_full) return 1;

    /* t: 0 at speed_min, 1 at speed_full. */
    float t = (speed - speed_min) / (speed_full - speed_min);

    /* Interval: max_interval at t=0, 1 at t=1. */
    float interval_f = (float)max_interval * (1.0f - t) + 1.0f * t;
    uint32_t interval = (uint32_t)(interval_f + 0.5f);
    if (interval < 1) interval = 1;
    return interval;
}

/* ── tick ───────────────────────────────────────────────────── */

uint32_t fr_priority_body_sender_tick(
    fr_priority_body_sender_t *s,
    const phys_world_t *world,
    const uint8_t *constrained_flags,
    uint64_t tick,
    net_udp_socket_t *sock,
    const net_udp_addr_t *addrs,
    const uint8_t *addr_active,
    uint16_t max_clients,
    const uint32_t *joint_pairs,
    uint32_t joint_pair_count)
{
    if (!s || !world || !constrained_flags || !sock) return 0;

    uint32_t cap = world->body_pool.capacity;
    if (cap > s->max_bodies) cap = s->max_bodies;

    /* ── Pass 1: compute per-body intervals from velocity. ──── */
    for (uint32_t bi = 0; bi < cap; bi++) {
        s->intervals[bi] = UINT32_MAX; /* default: skip */

        if (!constrained_flags[bi]) continue;
        if (!phys_body_pool_is_active(&world->body_pool, bi)) continue;

        const phys_body_t *body =
            phys_body_pool_get_curr((phys_body_pool_t *)&world->body_pool, bi);
        if (!body) continue;
        if (body->flags & PHYS_BODY_FLAG_SLEEPING) continue;

        float lin_sq = body->linear_vel.x * body->linear_vel.x
                     + body->linear_vel.y * body->linear_vel.y
                     + body->linear_vel.z * body->linear_vel.z;
        float ang_sq = body->angular_vel.x * body->angular_vel.x
                     + body->angular_vel.y * body->angular_vel.y
                     + body->angular_vel.z * body->angular_vel.z;
        float speed = sqrtf(lin_sq + ang_sq);

        s->intervals[bi] = interval_from_speed_(
            speed, s->speed_min, s->speed_full_rate, s->max_interval);
    }

    /* ── Pass 2: propagate min interval across joint partners. ─ */
    if (joint_pairs && joint_pair_count > 0) {
        for (uint32_t j = 0; j < joint_pair_count; j++) {
            uint32_t a = joint_pairs[j * 2u];
            uint32_t b = joint_pairs[j * 2u + 1u];
            if (a >= cap || b >= cap) continue;

            uint32_t best = s->intervals[a];
            if (s->intervals[b] < best) best = s->intervals[b];
            s->intervals[a] = best;
            s->intervals[b] = best;
        }
    }

    /* ── Pass 3: send bodies whose interval has elapsed. ─────── */
    uint32_t sent = 0;
    for (uint32_t bi = 0; bi < cap; bi++) {
        uint32_t interval = s->intervals[bi];
        if (interval == UINT32_MAX) continue;

        uint64_t elapsed = tick - s->last_send_tick[bi];
        if (elapsed < (uint64_t)interval) continue;
        s->last_send_tick[bi] = tick;

        const phys_body_t *body =
            phys_body_pool_get_curr((phys_body_pool_t *)&world->body_pool, bi);
        if (!body) continue;

        /* Encode BODY_STATE. */
        net_repl_body_state_t st;
        memset(&st, 0, sizeof(st));
        st.server_tick = (uint16_t)(tick & 0xFFFFu);
        st.body_id     = (uint16_t)bi;
        st.pos_mm.x_mm = (int32_t)(body->position.x * 1000.0f);
        st.pos_mm.y_mm = (int32_t)(body->position.y * 1000.0f);
        st.pos_mm.z_mm = (int32_t)(body->position.z * 1000.0f);
        st.rot_x = body->orientation.x;
        st.rot_y = body->orientation.y;
        st.rot_z = body->orientation.z;
        st.rot_w = body->orientation.w;
        st.vel_x_mm_s = (int16_t)fmaxf(-32767.0f,
            fminf(32767.0f, body->linear_vel.x * 1000.0f));
        st.vel_y_mm_s = (int16_t)fmaxf(-32767.0f,
            fminf(32767.0f, body->linear_vel.y * 1000.0f));
        st.vel_z_mm_s = (int16_t)fmaxf(-32767.0f,
            fminf(32767.0f, body->linear_vel.z * 1000.0f));
        st.ang_x_mrad_s = (int16_t)fmaxf(-32767.0f,
            fminf(32767.0f, body->angular_vel.x * 1000.0f));
        st.ang_y_mrad_s = (int16_t)fmaxf(-32767.0f,
            fminf(32767.0f, body->angular_vel.y * 1000.0f));
        st.ang_z_mrad_s = (int16_t)fmaxf(-32767.0f,
            fminf(32767.0f, body->angular_vel.z * 1000.0f));

        uint8_t msg[2u + NET_REPL_BODY_STATE_PAYLOAD_SIZE];
        msg[0] = (uint8_t)(NET_REPL_SCHEMA_BODY_STATE & 0xFFu);
        msg[1] = (uint8_t)((NET_REPL_SCHEMA_BODY_STATE >> 8u) & 0xFFu);
        if (net_repl_body_state_encode(&st, msg + 2u,
                                        NET_REPL_BODY_STATE_PAYLOAD_SIZE)
            != NET_REPL_OK) {
            continue;
        }

        for (uint16_t cid = 0; cid < max_clients; cid++) {
            if (!addr_active[cid]) continue;
            net_udp_socket_sendto(sock, &addrs[cid],
                                  msg, 2u + NET_REPL_BODY_STATE_PAYLOAD_SIZE);
        }
        sent++;
    }
    return sent;
}

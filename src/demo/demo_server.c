/**
 * @file demo_server.c
 * @brief Headless demo server binary.
 *
 * Creates a UDP socket, listens for up to 4 RUDP clients, processes
 * INPUT_MOVE and INPUT_SPAWN messages, ticks a physics world at a
 * fixed rate, and broadcasts body state (STATE_CUBE) to all clients.
 *
 * Usage: ./build/demo_server <port> [tick_hz] [workers]
 *   Default: port=40080 tick_hz=500 workers=4
 */

#define _POSIX_C_SOURCE 200809L

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <sched.h>
#include <time.h>

#include "ferrum/job/system.h"
#include "ferrum/net/rudp/peer.h"
#include "ferrum/net/replication/common.h"
#include "ferrum/net/replication/body_spawn.h"
#include "ferrum/net/replication/body_state.h"
#include "ferrum/net/udp_socket.h"
#include "ferrum/net/topic_channel.h"

#include "ferrum/server/entity/net/pump.h"
#include "ferrum/server/net/runtime.h"

#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/phys_tick_runner.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/manifold_cache.h"

#include "ferrum/demo/server_world.h"
#include "ferrum/demo/input_move.h"
#include "ferrum/demo/input_spawn.h"

/* ── Time helpers ───────────────────────────────────────────────── */

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

/** Wall-clock milliseconds (CLOCK_REALTIME) for cross-machine timestamps.
 *  Truncated to 32 bits in the wire format — wraps every ~49 days. */
static uint32_t wall_ms_(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t ms = (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
    return (uint32_t)(ms & 0xFFFFFFFFu);
}

static void sleep_ms(uint32_t ms) {
    struct timespec ts;
    ts.tv_sec  = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)((ms % 1000u) * 1000u * 1000u);
    nanosleep(&ts, NULL);
}

/* ── Signal handling ────────────────────────────────────────────── */

static volatile sig_atomic_t g_stop = 0;

static void handle_stop_signal(int signum) {
    (void)signum;
    g_stop = 1;
}

/* ── I/O counting wrappers ──────────────────────────────────────── */

struct demo_server_io {
    net_udp_socket_t *sock;
    atomic_uint_least64_t packets_sent;
    atomic_uint_least64_t packets_recv;
    atomic_uint_least64_t bytes_sent;
    atomic_uint_least64_t bytes_recv;
};

static int recvfrom_counting(void *user,
                             net_udp_addr_t *out_from,
                             uint8_t *out_data,
                             size_t out_cap,
                             size_t *out_size) {
    struct demo_server_io *io = (struct demo_server_io *)user;
    if (!io || !io->sock) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }
    int rc = net_udp_socket_recvfrom(io->sock, out_from, out_data, out_cap, out_size);
    if (rc == NET_UDP_SOCKET_OK && out_size) {
        atomic_fetch_add_explicit(&io->packets_recv, 1u, memory_order_relaxed);
        atomic_fetch_add_explicit(&io->bytes_recv, (uint64_t)(*out_size), memory_order_relaxed);
    }
    return rc;
}

static int sendto_counting(void *user, const net_udp_addr_t *to,
                           const void *data, size_t size) {
    struct demo_server_io *io = (struct demo_server_io *)user;
    if (!io || !io->sock) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }
    int rc = net_udp_socket_sendto(io->sock, to, data, size);
    if (rc == NET_UDP_SOCKET_OK) {
        atomic_fetch_add_explicit(&io->packets_sent, 1u, memory_order_relaxed);
        atomic_fetch_add_explicit(&io->bytes_sent, (uint64_t)size, memory_order_relaxed);
    }
    return rc;
}

/* ── Pump callback: resolve per-client outbound topics ──────────── */

static bool get_out_topics_from_runtime(void *user,
                                        uint16_t client_id,
                                        fr_topic_channel_t **out_rel,
                                        fr_topic_channel_t **out_unrel) {
    fr_server_net_runtime_t *rt = (fr_server_net_runtime_t *)user;
    if (!rt || !out_rel || !out_unrel) {
        return false;
    }
    return fr_server_net_runtime_client_out_topics(rt, client_id, out_rel, out_unrel);
}

/* ── Unreliable push helper ─────────────────────────────────────── */

static void push_unreliable_(fr_server_net_runtime_t *rt,
                             uint16_t client_id,
                             uint16_t schema_id,
                             const uint8_t *payload,
                             size_t payload_size) {
    if (!rt || !payload || payload_size == 0u) {
        return;
    }

    fr_topic_channel_t *out_rel  = NULL;
    fr_topic_channel_t *out_unrel = NULL;
    if (!fr_server_net_runtime_client_out_topics(rt, client_id,
                                                 &out_rel, &out_unrel) || !out_unrel) {
        return;
    }

    if (payload_size > NET_RUDP_MAX_PACKET_SIZE) {
        return;
    }

    uint8_t msg[2u + NET_RUDP_MAX_PACKET_SIZE];
    msg[0] = (uint8_t)(schema_id & 0xFFu);
    msg[1] = (uint8_t)((schema_id >> 8u) & 0xFFu);
    memcpy(msg + 2u, payload, payload_size);
    (void)fr_topic_channel_push(out_unrel, msg, 2u + payload_size);
}

/* ── Reliable push helper ──────────────────────────────────────── */

static void push_reliable_(fr_server_net_runtime_t *rt,
                            uint16_t client_id,
                            uint16_t schema_id,
                            const uint8_t *payload,
                            size_t payload_size) {
    if (!rt || !payload || payload_size == 0u) {
        return;
    }

    fr_topic_channel_t *out_rel  = NULL;
    fr_topic_channel_t *out_unrel = NULL;
    if (!fr_server_net_runtime_client_out_topics(rt, client_id,
                                                 &out_rel, &out_unrel) || !out_rel) {
        return;
    }

    if (payload_size > NET_RUDP_MAX_PACKET_SIZE) {
        return;
    }

    uint8_t msg[2u + NET_RUDP_MAX_PACKET_SIZE];
    msg[0] = (uint8_t)(schema_id & 0xFFu);
    msg[1] = (uint8_t)((schema_id >> 8u) & 0xFFu);
    memcpy(msg + 2u, payload, payload_size);
    (void)fr_topic_channel_push(out_rel, msg, 2u + payload_size);
}

/* ── Broadcast body states to all connected clients ─────────────── */

/** Maximum number of high-priority bodies sent reliably per tick. */
#define DEMO_RELIABLE_BODY_BUDGET 16u

/** Maximum total bodies broadcast per tick regardless of awake count.
 *  The fastest-moving bodies are prioritized.  Bodies beyond this cap
 *  simply wait for the next tick.  This prevents bandwidth explosion
 *  as body count grows. */
#define DEMO_SEND_BUDGET_PER_TICK 256u

/** Maximum number of BODY_SPAWN messages sent per tick per client.
 *
 * Newly-joined clients may otherwise miss already-sleeping bodies entirely
 * (they won't enter the "awake" ranked list), resulting in a connected
 * client that sees an empty world. */
#define DEMO_SPAWN_BUDGET_PER_TICK 64u

/** Entry used by broadcast_body_states_ to collect sendable bodies. */
struct body_rank_ {
    uint32_t index;
    float    speed_sq;   /* linear speed squared for sorting */
};

/** Simple insertion sort (N is small, typically < 128). */
static void sort_by_speed_desc_(struct body_rank_ *arr, uint32_t n) {
    for (uint32_t i = 1u; i < n; ++i) {
        struct body_rank_ key = arr[i];
        uint32_t j = i;
        while (j > 0u && arr[j - 1u].speed_sq < key.speed_sq) {
            arr[j] = arr[j - 1u];
            --j;
        }
        arr[j] = key;
    }
}

/** Bitset helpers for per-client body_known tracking. */
static int bk_test_(const uint8_t *bits, uint32_t idx) {
    return (bits[idx >> 3u] >> (idx & 7u)) & 1u;
}
static void bk_set_(uint8_t *bits, uint32_t idx) {
    bits[idx >> 3u] |= (uint8_t)(1u << (idx & 7u));
}

/** Read shape half-extents from the collider/shape pools.  Writes to
 *  out_x/y/z in millimeters.  Returns 0 on success. */
static int read_half_mm_(const phys_world_t *w, uint32_t bi,
                         uint16_t *ox, uint16_t *oy, uint16_t *oz) {
    const phys_collider_t *c = &w->colliders[bi];
    switch (c->type) {
    case PHYS_SHAPE_BOX: {
        const phys_box_t *box = &w->boxes[c->shape_index];
        *ox = (uint16_t)(box->half_extents.x * 1000.0f);
        *oy = (uint16_t)(box->half_extents.y * 1000.0f);
        *oz = (uint16_t)(box->half_extents.z * 1000.0f);
        return 0;
    }
    case PHYS_SHAPE_SPHERE: {
        const phys_sphere_t *s = &w->spheres[c->shape_index];
        *ox = (uint16_t)(s->radius * 1000.0f);
        *oy = 0;
        *oz = 0;
        return 0;
    }
    case PHYS_SHAPE_CAPSULE: {
        const phys_capsule_t *cap = &w->capsules[c->shape_index];
        *ox = (uint16_t)(cap->radius * 1000.0f);
        *oy = (uint16_t)(cap->half_height * 1000.0f);
        *oz = 0;
        return 0;
    }
    default:
        *ox = *oy = *oz = 0;
        return -1;
    }
}

/** Clamp a float velocity (m/s) to int16 (mm/s, ±32 m/s). */
static int16_t vel_to_mm_s_(float v) {
    float mm = v * 1000.0f;
    if (mm >  32767.0f) mm =  32767.0f;
    if (mm < -32767.0f) mm = -32767.0f;
    return (int16_t)mm;
}

/** Quantize angular velocity (rad/s) to mrad/s as int16. */
static int16_t angvel_to_mrad_s_(float v) {
    float mr = v * 1000.0f;
    if (mr >  32767.0f) mr =  32767.0f;
    if (mr < -32767.0f) mr = -32767.0f;
    return (int16_t)mr;
}

/* ── Collision prediction for extrapolation gating ─────────────── */

/** Prediction window for AABB sweep / raycast (seconds). */
#define COLLISION_PREDICT_WINDOW_S 0.05f

/** Build a bitset of body indices that are currently in the manifold
 *  cache (i.e. have active contacts this tick). */
static void build_colliding_bitset_(const phys_manifold_cache_t *cache,
                                    uint8_t *bits, uint32_t capacity) {
    if (!cache || !bits) return;
    memset(bits, 0, (capacity + 7u) / 8u);
    for (uint32_t i = 0; i < cache->count; ++i) {
        const phys_manifold_cache_entry_t *e = &cache->entries[i];
        if (e->manifold.point_count == 0) continue;
        uint32_t a = e->manifold.body_a;
        uint32_t b = e->manifold.body_b;
        if (a < capacity) bits[a >> 3] |= (uint8_t)(1u << (a & 7u));
        if (b < capacity) bits[b >> 3] |= (uint8_t)(1u << (b & 7u));
    }
}

/** Test whether a ray (origin, direction, max_t) hits an AABB.
 *  Uses slab method.  Returns true if intersection exists at t < max_t. */
static int ray_hits_aabb_(phys_vec3_t origin, phys_vec3_t inv_dir,
                          const phys_aabb_t *box, float max_t) {
    float tmin = 0.0f, tmax = max_t;
    const float *o  = &origin.x;
    const float *id = &inv_dir.x;
    const float *bmin = &box->min.x;
    const float *bmax = &box->max.x;
    for (int i = 0; i < 3; ++i) {
        float t1 = (bmin[i] - o[i]) * id[i];
        float t2 = (bmax[i] - o[i]) * id[i];
        if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
        if (t1 > tmin) tmin = t1;
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return 0;
    }
    return 1;
}

/** Predict whether body bi will collide within the prediction window.
 *
 *  Strategy varies by tier:
 *    T0-T2: expand AABB by velocity * window, test overlap vs all others.
 *    T3:    cast a ray from body center along velocity, test vs all AABBs.
 *    T4:    always returns 1 (no extrapolation for background bodies).
 *
 *  Also returns 1 if the body is already colliding (bitset). */
static int predict_collision_(const phys_world_t *world, uint32_t bi,
                              const uint8_t *colliding_bits, float window_s) {
    /* Already in contact — definitely colliding. */
    if ((colliding_bits[bi >> 3] >> (bi & 7u)) & 1u) return 1;

    const phys_body_t *b = phys_world_get_body((phys_world_t *)(uintptr_t)world, bi);
    if (!b) return 1;

    /* T4 background: always flag colliding → client won't extrapolate. */
    if (b->tier >= 4u) return 1;

    const phys_aabb_t *my_aabb = phys_world_get_aabb(world, bi);
    if (!my_aabb) return 1;

    const uint32_t cap = world->body_pool.capacity;
    const float vx = b->linear_vel.x, vy = b->linear_vel.y, vz = b->linear_vel.z;
    const float speed_sq = vx*vx + vy*vy + vz*vz;

    /* Nearly stationary — no collision prediction needed. */
    if (speed_sq < 0.01f) return 0;

    if (b->tier <= 2u) {
        /* T0-T2: swept AABB overlap test. */
        phys_aabb_t swept = *my_aabb;
        float dx = vx * window_s, dy = vy * window_s, dz = vz * window_s;
        if (dx > 0) swept.max.x += dx; else swept.min.x += dx;
        if (dy > 0) swept.max.y += dy; else swept.min.y += dy;
        if (dz > 0) swept.max.z += dz; else swept.min.z += dz;

        for (uint32_t j = 0; j < cap; ++j) {
            if (j == bi) continue;
            if (!phys_body_pool_is_active(&world->body_pool, j)) continue;
            const phys_aabb_t *other = phys_world_get_aabb(world, j);
            if (!other) continue;
            if (phys_aabb_overlap(&swept, other)) return 1;
        }
    } else {
        /* T3: raycast along velocity direction vs all AABBs. */
        float inv_speed = 1.0f / sqrtf(speed_sq);
        phys_vec3_t dir = {vx * inv_speed, vy * inv_speed, vz * inv_speed};
        float max_t = sqrtf(speed_sq) * window_s;
        /* Compute inverse direction for slab test (avoid div-by-zero). */
        phys_vec3_t inv_dir = {
            (fabsf(dir.x) > 1e-8f) ? 1.0f / dir.x : 1e8f,
            (fabsf(dir.y) > 1e-8f) ? 1.0f / dir.y : 1e8f,
            (fabsf(dir.z) > 1e-8f) ? 1.0f / dir.z : 1e8f,
        };
        phys_vec3_t origin = b->position;

        for (uint32_t j = 0; j < cap; ++j) {
            if (j == bi) continue;
            if (!phys_body_pool_is_active(&world->body_pool, j)) continue;
            const phys_aabb_t *other = phys_world_get_aabb(world, j);
            if (!other) continue;
            if (ray_hits_aabb_(origin, inv_dir, other, max_t)) return 1;
        }
    }
    return 0;
}

static void broadcast_body_states_(fr_server_net_runtime_t *rt,
                                   demo_server_world_t *sw,
                                   const uint8_t *client_connected,
                                   uint16_t max_clients,
                                   uint16_t server_tick) {
    if (!rt || !sw || !client_connected || max_clients == 0u) {
        return;
    }

    /* ── Pass 1: collect bodies that need state updates this tick.
     *    - All awake bodies are sent every tick.
     *    - Bodies that just fell asleep get one final correction so the
     *      client can mark them sleeping too.
     *    - Already-sleeping bodies are skipped. ─────────────────────── */
    const uint32_t capacity = sw->physics.body_pool.capacity;

    struct body_rank_ ranked[512];
    uint32_t awake_count = 0u;

    for (uint32_t i = 0u; i < capacity; ++i) {
        if (i == DEMO_GROUND_BODY) {
            continue;
        }
        if (!phys_body_pool_is_active(&sw->physics.body_pool, i)) {
            continue;
        }
        const phys_body_t *b = phys_world_get_body(&sw->physics, i);
        if (!b) {
            continue;
        }

        /* Include kinematic bodies (players) — they need spawn/state too.
         * Skip fully static bodies (ground is already excluded above). */
        if (phys_body_is_static(b) && !phys_body_is_kinematic(b)) {
            continue;
        }

        const int sleeping_now = phys_body_is_sleeping(b);
        const int was_sleeping = (sw->body_was_sleeping[i / 8u] >> (i & 7u)) & 1u;

        if (sleeping_now && was_sleeping) {
            /* Already sleeping, already notified — skip. */
            continue;
        }

        /* Update the was-sleeping bitset. */
        if (sleeping_now) {
            sw->body_was_sleeping[i / 8u] |= (uint8_t)(1u << (i & 7u));
        } else {
            sw->body_was_sleeping[i / 8u] &= (uint8_t)~(1u << (i & 7u));
        }

        float spd_sq = 0.0f;
        if (!sleeping_now) {
            float sx = b->linear_vel.x;
            float sy = b->linear_vel.y;
            float sz = b->linear_vel.z;
            spd_sq = sx * sx + sy * sy + sz * sz;
        }

        if (awake_count < 512u) {
            ranked[awake_count].index    = i;
            ranked[awake_count].speed_sq = spd_sq;
            awake_count++;
        }
    }

    //sort_by_speed_desc_(ranked, awake_count);

    /* Build collision-prediction bitset from manifold cache +
     * per-body AABB sweep / raycast (tier-dependent). */
    uint8_t coll_bits[(512 + 7) / 8];
    build_colliding_bitset_(&sw->physics.manifold_cache, coll_bits, capacity);

    const uint32_t send_time = wall_ms_();

    /* ── Pass 2: for each client, send BODY_SPAWN for unknown bodies,
     *            then BODY_STATE for known awake bodies ──────────── */
    uint32_t send_count = awake_count;
    if (send_count > DEMO_SEND_BUDGET_PER_TICK) {
        send_count = DEMO_SEND_BUDGET_PER_TICK;
    }

    for (uint16_t ci = 0u; ci < max_clients; ++ci) {
        if (!client_connected[ci]) {
            continue;
        }

        uint8_t *known = sw->body_known[ci];

        /* Ensure newly-joined clients eventually see *all* bodies, even if
         * they are already sleeping (and thus won't be present in ranked[]). */
        uint32_t spawn_sent = 0u;
        for (uint32_t bi = 0u; bi < capacity && spawn_sent < DEMO_SPAWN_BUDGET_PER_TICK; ++bi) {
            if (bi == DEMO_GROUND_BODY) {
                continue;
            }
            if (!phys_body_pool_is_active(&sw->physics.body_pool, bi)) {
                continue;
            }
            if (bk_test_(known, bi)) {
                continue;
            }

            const phys_body_t *b = phys_world_get_body(&sw->physics, bi);
            if (!b) {
                continue;
            }
            if (phys_body_is_static(b) && !phys_body_is_kinematic(b)) {
                continue;
            }

            net_repl_body_spawn_t sp;
            memset(&sp, 0, sizeof(sp));
            sp.body_id    = (uint16_t)bi;
            sp.flags      = (uint8_t)b->flags;
            sp.shape_type = sw->body_shape_type[bi];
            sp.color_seed = sw->body_color_seed[bi];

            sp.pos_mm.x_mm = (int32_t)(b->position.x * 1000.0f);
            sp.pos_mm.y_mm = (int32_t)(b->position.y * 1000.0f);
            sp.pos_mm.z_mm = (int32_t)(b->position.z * 1000.0f);

            sp.rot_x = b->orientation.x;
            sp.rot_y = b->orientation.y;
            sp.rot_z = b->orientation.z;
            sp.rot_w = b->orientation.w;

            read_half_mm_(&sw->physics, bi,
                          &sp.half_x_mm, &sp.half_y_mm, &sp.half_z_mm);

            uint8_t payload[NET_REPL_BODY_SPAWN_PAYLOAD_SIZE];
            if (net_repl_body_spawn_encode(&sp, payload, sizeof(payload)) == NET_REPL_OK) {
                push_reliable_(rt, ci, NET_REPL_SCHEMA_BODY_SPAWN,
                               payload, sizeof(payload));
                bk_set_(known, bi);
                spawn_sent++;
            }
        }

        for (uint32_t ri = 0u; ri < send_count; ++ri) {
            const uint32_t bi = ranked[ri].index;
            const phys_body_t *b = phys_world_get_body(&sw->physics, bi);
            if (!b) {
                continue;
            }

            /* If client hasn't seen this body yet, send BODY_SPAWN. */
            if (!bk_test_(known, bi)) {
                net_repl_body_spawn_t sp;
                memset(&sp, 0, sizeof(sp));
                sp.body_id    = (uint16_t)bi;
                sp.flags      = (uint8_t)b->flags;
                sp.shape_type = sw->body_shape_type[bi];
                sp.color_seed = sw->body_color_seed[bi];

                /* Quantize position. */
                sp.pos_mm.x_mm = (int32_t)(b->position.x * 1000.0f);
                sp.pos_mm.y_mm = (int32_t)(b->position.y * 1000.0f);
                sp.pos_mm.z_mm = (int32_t)(b->position.z * 1000.0f);

                /* Orientation. */
                sp.rot_x = b->orientation.x;
                sp.rot_y = b->orientation.y;
                sp.rot_z = b->orientation.z;
                sp.rot_w = b->orientation.w;

                /* Shape extents from collider. */
                read_half_mm_(&sw->physics, bi,
                              &sp.half_x_mm, &sp.half_y_mm, &sp.half_z_mm);

                uint8_t payload[NET_REPL_BODY_SPAWN_PAYLOAD_SIZE];
                if (net_repl_body_spawn_encode(&sp, payload, sizeof(payload))
                    == NET_REPL_OK) {
                    push_reliable_(rt, ci, NET_REPL_SCHEMA_BODY_SPAWN,
                                   payload, sizeof(payload));
                    bk_set_(known, bi);
                }
                continue; /* State will follow next tick. */
            }

            /* Build BODY_STATE. */
            net_repl_body_state_t st;
            memset(&st, 0, sizeof(st));
            st.server_tick = server_tick;
            st.body_id     = (uint16_t)bi;

            st.pos_mm.x_mm = (int32_t)(b->position.x * 1000.0f);
            st.pos_mm.y_mm = (int32_t)(b->position.y * 1000.0f);
            st.pos_mm.z_mm = (int32_t)(b->position.z * 1000.0f);

            st.rot_x = b->orientation.x;
            st.rot_y = b->orientation.y;
            st.rot_z = b->orientation.z;
            st.rot_w = b->orientation.w;

            st.vel_x_mm_s = vel_to_mm_s_(b->linear_vel.x);
            st.vel_y_mm_s = vel_to_mm_s_(b->linear_vel.y);
            st.vel_z_mm_s = vel_to_mm_s_(b->linear_vel.z);

            st.ang_x_mrad_s = angvel_to_mrad_s_(b->angular_vel.x);
            st.ang_y_mrad_s = angvel_to_mrad_s_(b->angular_vel.y);
            st.ang_z_mrad_s = angvel_to_mrad_s_(b->angular_vel.z);

            st.send_time_ms = send_time;

            /* Pack tier + colliding flag.  For T0-T3 the collision
             * predictor does an AABB sweep (T0-T2) or raycast (T3).
             * T4 is always flagged colliding so the client skips
             * extrapolation for background bodies. */
            uint8_t sf = 0;
            sf = net_repl_body_state_set_tier(sf, b->tier);
            if (predict_collision_(&sw->physics, bi, coll_bits,
                                   COLLISION_PREDICT_WINDOW_S)) {
                sf |= NET_REPL_BODY_STATE_FLAG_COLLIDING;
            }
            st.flags = sf;

            uint8_t payload[NET_REPL_BODY_STATE_PAYLOAD_SIZE];
            if (net_repl_body_state_encode(&st, payload, sizeof(payload))
                != NET_REPL_OK) {
                continue;
            }

            const int use_reliable = false;//(ri < DEMO_RELIABLE_BODY_BUDGET);
            if (use_reliable) {
                push_reliable_(rt, ci, NET_REPL_SCHEMA_BODY_STATE,
                               payload, sizeof(payload));
            } else {
                push_unreliable_(rt, ci, NET_REPL_SCHEMA_BODY_STATE,
                                 payload, sizeof(payload));
            }
        }
    }
}

/* ── Dedicated network pump thread ──────────────────────────────── */

/** Arguments for the network pump thread. */
struct net_pump_thread_args {
    fr_server_net_runtime_t *rt;
    volatile sig_atomic_t   *stop;
};

/**
 * @brief Thread entry point: receives UDP packets and routes them to
 *        per-client fibers independently of the physics/gameplay loop.
 */
static void *net_pump_thread_fn(void *arg) {
    struct net_pump_thread_args *a = (struct net_pump_thread_args *)arg;
    struct timespec ts = {0, 1000000}; /* 1 ms */

    while (!(*a->stop)) {
        (void)fr_server_net_runtime_pump(a->rt, now_ms());
        nanosleep(&ts, NULL);
    }
    return NULL;
}

/* ── Usage ──────────────────────────────────────────────────────── */

static void usage(const char *argv0) {
    fprintf(stderr,
            "Usage: %s <port> [tick_hz] [sim_workers] [net_workers]\n"
            "  Default: port=40080 tick_hz=500 sim_workers=4 net_workers=1\n"
            "  net_workers: dedicated threads for networking fibers\n",
            argv0);
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    if (argc < 2 || argc > 5) {
        usage(argv[0]);
        return 2;
    }

    /* Parse arguments. */
    long port_l        = strtol(argv[1], NULL, 10);
    long tick_hz_l     = (argc >= 3) ? strtol(argv[2], NULL, 10) : 60;
    long workers_l     = (argc >= 4) ? strtol(argv[3], NULL, 10) : 4;
    long net_workers_l = (argc >= 5) ? strtol(argv[4], NULL, 10) : 1;

    if (port_l < 1 || port_l > 65535 || tick_hz_l <= 0 || workers_l <= 0 ||
        net_workers_l <= 0) {
        fprintf(stderr, "Invalid arguments\n");
        return 2;
    }

    const uint16_t max_clients = DEMO_MAX_CLIENTS;

    /* ── Signal handling ──────────────────────────────────────────── */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_stop_signal;
    (void)sigaction(SIGINT, &sa, NULL);
    (void)sigaction(SIGTERM, &sa, NULL);

    /* ── UDP socket ───────────────────────────────────────────────── */
    net_udp_socket_t sock;
    if (net_udp_socket_open(&sock) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "Failed to open UDP socket\n");
        return 1;
    }

    net_udp_addr_t bind_addr;
    if (net_udp_addr_ipv4(&bind_addr, 0u, 0u, 0u, 0u,
                           (uint16_t)port_l) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "Failed to build bind address\n");
        net_udp_socket_close(&sock);
        return 1;
    }
    if (net_udp_socket_bind(&sock, &bind_addr) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "Failed to bind port %ld: %s\n", port_l, strerror(errno));
        net_udp_socket_close(&sock);
        return 1;
    }

    (void)net_udp_socket_set_recv_buffer_bytes(&sock, 4u * 1024u * 1024u);
    (void)net_udp_socket_set_send_buffer_bytes(&sock, 4u * 1024u * 1024u);
    (void)net_udp_socket_set_nonblocking(&sock, 1);

    fprintf(stderr, "demo_server: listening on port %ld  tick_hz=%ld"
            "  sim_workers=%ld  net_workers=%ld\n",
            port_l, tick_hz_l, workers_l, net_workers_l);

    /* ── Job systems ─────────────────────────────────────────────── */

    /* Simulation job system: runs physics stages and gameplay jobs. */
    job_system_t jobs;
    job_system_create_status_t jstatus =
        job_system_create(&jobs, (uint32_t)workers_l, 4096u, 1u << 18, 2048, 0);
    if (jstatus != JOB_CREATE_OK) {
        fprintf(stderr, "Failed to create simulation job system\n");
        net_udp_socket_close(&sock);
        return 1;
    }
    if (job_system_start(&jobs) != 0) {
        fprintf(stderr, "Failed to start simulation job system\n");
        job_system_shutdown(&jobs);
        net_udp_socket_close(&sock);
        return 1;
    }

    /* Dedicated networking job system: runs per-client RUDP fibers on
     * their own OS threads so that physics tick latency can never
     * starve network I/O. */
    job_system_t net_jobs;
    job_system_create_status_t njstatus =
        job_system_create(&net_jobs, (uint32_t)net_workers_l, 1024u,
                          1u << 18, 256, 0);
    if (njstatus != JOB_CREATE_OK) {
        fprintf(stderr, "Failed to create network job system\n");
        job_system_shutdown(&jobs);
        net_udp_socket_close(&sock);
        return 1;
    }
    if (job_system_start(&net_jobs) != 0) {
        fprintf(stderr, "Failed to start network job system\n");
        job_system_shutdown(&net_jobs);
        job_system_shutdown(&jobs);
        net_udp_socket_close(&sock);
        return 1;
    }

    /* ── Topic channels ───────────────────────────────────────────── */
    fr_topic_channel_config_t tcfg;
    memset(&tcfg, 0, sizeof(tcfg));
    tcfg.capacity = 262144u;

    fr_topic_channel_t *inbound       = fr_topic_channel_create(&tcfg);
    fr_topic_channel_t *player_events = fr_topic_channel_create(&tcfg);
    fr_topic_channel_t *entity_events = fr_topic_channel_create(&tcfg);

    /* Command channel for deferred physics mutations.  Sized to hold
     * a full stack spawn burst (~50 commands × ~128 bytes each). */
    fr_topic_channel_config_t cmd_tcfg;
    memset(&cmd_tcfg, 0, sizeof(cmd_tcfg));
    cmd_tcfg.capacity = 65536u;
    fr_topic_channel_t *physics_cmds  = fr_topic_channel_create(&cmd_tcfg);

    if (!inbound || !player_events || !entity_events || !physics_cmds) {
        fprintf(stderr, "Failed to allocate topic channels\n");
        fr_topic_channel_destroy(physics_cmds);
        fr_topic_channel_destroy(inbound);
        fr_topic_channel_destroy(player_events);
        fr_topic_channel_destroy(entity_events);
        job_system_shutdown(&net_jobs);
        job_system_shutdown(&jobs);
        net_udp_socket_close(&sock);
        return 1;
    }

    /* ── I/O wrapper ──────────────────────────────────────────────── */
    struct demo_server_io io;
    memset(&io, 0, sizeof(io));
    io.sock = &sock;

    /* ── Server net runtime ───────────────────────────────────────── */
    fr_server_net_runtime_config_t rt_cfg;
    memset(&rt_cfg, 0, sizeof(rt_cfg));
    rt_cfg.max_clients            = max_clients;
    rt_cfg.jobs                   = &net_jobs;
    rt_cfg.socket                 = &sock;
    rt_cfg.inbound_topic          = inbound;
    rt_cfg.out_reliable_capacity  = 8192u;
    rt_cfg.out_unreliable_capacity = 8192u;
    rt_cfg.recvfrom_cb            = recvfrom_counting;
    rt_cfg.sendto_cb              = sendto_counting;
    rt_cfg.io_user                = &io;

    fr_server_net_runtime_t *rt = fr_server_net_runtime_create(&rt_cfg);
    if (!rt) {
        fprintf(stderr, "Failed to create server net runtime\n");
        fr_topic_channel_destroy(inbound);
        fr_topic_channel_destroy(player_events);
        fr_topic_channel_destroy(entity_events);
        job_system_shutdown(&net_jobs);
        job_system_shutdown(&jobs);
        net_udp_socket_close(&sock);
        return 1;
    }

    /* ── Entity net pump ──────────────────────────────────────────── */
    fr_server_entity_net_pump_config_t pump_cfg;
    memset(&pump_cfg, 0, sizeof(pump_cfg));
    pump_cfg.max_clients             = max_clients;
    pump_cfg.tick_hz                 = (uint16_t)tick_hz_l;
    pump_cfg.expected_entities       = DEMO_MAX_BODIES;
    pump_cfg.inbound_topic           = inbound;
    pump_cfg.player_event_topic      = player_events;
    pump_cfg.entity_event_topic      = entity_events;
    pump_cfg.get_client_out_topics_cb = get_out_topics_from_runtime;
    pump_cfg.io_user                 = rt;

    fr_server_entity_net_pump_t *pump = fr_server_entity_net_pump_create(&pump_cfg);
    if (!pump) {
        fprintf(stderr, "Failed to create entity net pump\n");
        fr_server_net_runtime_destroy(rt);
        fr_topic_channel_destroy(inbound);
        fr_topic_channel_destroy(player_events);
        fr_topic_channel_destroy(entity_events);
        job_system_shutdown(&net_jobs);
        job_system_shutdown(&jobs);
        net_udp_socket_close(&sock);
        return 1;
    }

    /* ── Demo server world ────────────────────────────────────────── */
    demo_server_world_t sw;
    if (demo_server_world_init(&sw, 0u) != 0) {
        fprintf(stderr, "Failed to initialize demo server world\n");
        fr_server_entity_net_pump_destroy(pump);
        fr_server_net_runtime_destroy(rt);
        fr_topic_channel_destroy(physics_cmds);
        fr_topic_channel_destroy(inbound);
        fr_topic_channel_destroy(player_events);
        fr_topic_channel_destroy(entity_events);
        job_system_shutdown(&net_jobs);
        job_system_shutdown(&jobs);
        net_udp_socket_close(&sock);
        return 1;
    }
    sw.cmd_channel = physics_cmds;

    /* Per-client connection tracking. */
    uint8_t client_connected[DEMO_MAX_CLIENTS];
    memset(client_connected, 0, sizeof(client_connected));
    uint32_t clients_joined = 0u;

    /* ── Physics job context (parallel tick) ──────────────────────── */
    phys_job_context_t phys_jobs;
    phys_job_context_init(&phys_jobs, &jobs);

    /* ── Dedicated network receive thread ─────────────────────────── */
    struct net_pump_thread_args pump_thr_args = { .rt = rt, .stop = &g_stop };
    pthread_t net_pump_tid;
    if (pthread_create(&net_pump_tid, NULL, net_pump_thread_fn, &pump_thr_args) != 0) {
        fprintf(stderr, "Failed to start network pump thread\n");
        phys_job_context_destroy(&phys_jobs);
        demo_server_world_destroy(&sw);
        fr_server_entity_net_pump_destroy(pump);
        fr_server_net_runtime_destroy(rt);
        fr_topic_channel_destroy(physics_cmds);
        fr_topic_channel_destroy(inbound);
        fr_topic_channel_destroy(player_events);
        fr_topic_channel_destroy(entity_events);
        job_system_shutdown(&net_jobs);
        job_system_shutdown(&jobs);
        net_udp_socket_close(&sock);
        return 1;
    }

    /* ── Main loop ────────────────────────────────────────────────── */
    const float    dt_s     = 1.0f / (float)tick_hz_l;
    uint16_t       server_tick = 0u;

    fprintf(stderr, "DEMO_SERVER_READY\n");

    while (!g_stop) {
        uint64_t now = now_ms();

        /* (a) Route inbound messages through entity pump.
         *     The network receive pump runs on a dedicated thread;
         *     we only drain decoded messages here. */
        (void)fr_server_entity_net_pump_tick(pump, now);

        /* (b) Drain player events (JOIN). */
        for (;;) {
            uint8_t evt[32];
            size_t evt_len = sizeof(evt);
            if (!fr_topic_channel_pop(player_events, evt, &evt_len)) {
                break;
            }
            if (evt_len < 6u) {
                continue;
            }
            if (evt[0] != FR_SERVER_EVT_PLAYER_JOIN) {
                continue;
            }
            uint16_t client_id = (uint16_t)evt[2] | ((uint16_t)evt[3] << 8u);
            if (client_id >= max_clients) {
                continue;
            }
            if (!client_connected[client_id]) {
                client_connected[client_id] = 1u;
                clients_joined += 1u;

                /* Register player in physics world. */
                int slot = demo_server_world_add_player(&sw);
                (void)slot;
                fprintf(stderr, "demo_server: client %u joined (slot %d), %u total\n",
                        (unsigned)client_id, slot, clients_joined);
            }
        }

        /* (c) Drain entity events (INPUT_MOVE, INPUT_SPAWN). */
        for (;;) {
            uint8_t evt[64];
            size_t evt_len = sizeof(evt);
            if (!fr_topic_channel_pop(entity_events, evt, &evt_len)) {
                break;
            }
            if (evt_len < 4u) {
                continue;
            }

            const uint8_t evt_type = evt[0];
            const uint16_t src_client_id = (uint16_t)evt[2] | ((uint16_t)evt[3] << 8u);

            if (src_client_id >= max_clients || !client_connected[src_client_id]) {
                continue;
            }

            const uint8_t *payload   = evt + 4u;
            const size_t payload_len = evt_len - 4u;

            if (evt_type == (uint8_t)FR_SERVER_EVT_ENTITY_INPUT_MOVE) {
                demo_input_move_t input;
                if (demo_input_move_decode(&input, payload, payload_len) != NET_REPL_OK) {
                    continue;
                }
                demo_server_world_apply_input(&sw, (int)src_client_id, &input, dt_s);
            } else if (evt_type == (uint8_t)FR_SERVER_EVT_ENTITY_INPUT_SPAWN) {
                demo_input_spawn_t spawn;
                if (demo_input_spawn_decode(&spawn, payload, payload_len) != NET_REPL_OK) {
                    continue;
                }
                (void)demo_server_world_spawn_box(&sw, (int)src_client_id, &spawn);
            }
            /* Other entity events (INPUT_ROT etc.) are ignored in this demo. */
        }

        /* (d) Physics runs continuously on its own fiber.  We call
         *     demo_server_world_tick each iteration — it starts the
         *     runner on the first call (kick → start), and on later
         *     calls it's a no-op for physics but still spawns boxes. */
        demo_server_world_tick(&sw, &phys_jobs);

        #define BROADCAST_INTERVAL_MS (dt_s*1000.0)
        {
            static uint64_t next_broadcast = 0u;
            if (next_broadcast == 0u) { next_broadcast = now; }
            if (clients_joined > 0u && now >= next_broadcast) {
                server_tick = (uint16_t)(server_tick + 1u);
                broadcast_body_states_(rt, &sw, client_connected,
                                       max_clients, server_tick);
                next_broadcast += BROADCAST_INTERVAL_MS;
                if (now - next_broadcast > BROADCAST_INTERVAL_MS * 3u) {
                    next_broadcast = now;
                }
            }
        }

        /* Status line every ~1 second. */
        {
            static uint64_t next_status = 0u;
            if (next_status == 0u) { next_status = now + 1000u; }
            if (now >= next_status) {
                uint32_t body_count = phys_world_body_count(&sw.physics);
                /* Show first dynamic body position for debugging. */
                const phys_body_t *dbg_b = NULL;
                uint32_t dbg_idx = 0;
                for (uint32_t di = 2; di < sw.physics.body_pool.capacity; ++di) {
                    if (!phys_body_pool_is_active(&sw.physics.body_pool, di)) continue;
                    const phys_body_t *tb = phys_world_get_body(&sw.physics, di);
                    if (tb && !phys_body_is_static(tb) && !phys_body_is_kinematic(tb)) {
                        dbg_b = tb; dbg_idx = di; break;
                    }
                }
                fprintf(stderr, "tick %lu: %u bodies, %u clients  "
                        "rx=%lu tx=%lu",
                        (unsigned long)phys_tick_runner_tick_id(
                            &sw.tick_runner),
                        (unsigned)body_count,
                        (unsigned)clients_joined,
                        (unsigned long)atomic_load_explicit(&io.packets_recv,
                                                           memory_order_relaxed),
                        (unsigned long)atomic_load_explicit(&io.packets_sent,
                                                           memory_order_relaxed));
                if (dbg_b) {
                    fprintf(stderr, "  b%u=(%.2f,%.2f,%.2f v=%.2f,%.2f,%.2f t%u%s)",
                            dbg_idx,
                            (double)dbg_b->position.x,
                            (double)dbg_b->position.y,
                            (double)dbg_b->position.z,
                            (double)dbg_b->linear_vel.x,
                            (double)dbg_b->linear_vel.y,
                            (double)dbg_b->linear_vel.z,
                            (unsigned)dbg_b->tier,
                            phys_body_is_sleeping(dbg_b) ? " ZZZ" : "");
                }
                fprintf(stderr, "\n");
                next_status = now + 1000u;
            }
        }

        /* (f) Sleep briefly to avoid busy-spinning the main thread. */
        //sleep_ms(1u);
    }

    /* ── Cleanup ──────────────────────────────────────────────────── */
    fprintf(stderr, "demo_server: shutting down (tick %u)\n", (unsigned)server_tick);

    /* Wait for any in-flight physics tick before tearing down. */
    demo_server_world_tick_wait(&sw);

    /* Stop and join the network pump thread. */
    (void)pthread_join(net_pump_tid, NULL);

    phys_job_context_destroy(&phys_jobs);
    demo_server_world_destroy(&sw);
    fr_server_entity_net_pump_destroy(pump);
    fr_server_net_runtime_destroy(rt);
    fr_topic_channel_destroy(physics_cmds);
    fr_topic_channel_destroy(inbound);
    fr_topic_channel_destroy(player_events);
    fr_topic_channel_destroy(entity_events);
    job_system_shutdown(&net_jobs);
    job_system_shutdown(&jobs);
    net_udp_socket_close(&sock);

    fprintf(stderr, "demo_server: clean shutdown. pkts_in=%llu pkts_out=%llu\n",
            (unsigned long long)atomic_load_explicit(&io.packets_recv,
                                                     memory_order_relaxed),
            (unsigned long long)atomic_load_explicit(&io.packets_sent,
                                                     memory_order_relaxed));
    return 0;
}

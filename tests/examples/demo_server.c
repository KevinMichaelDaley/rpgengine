/**
 * @file demo_server.c
 * @brief Full-stack physics + replication demo server.
 *
 * Spawns a ground plane and periodically rains stacks of boxes.
 * Physics runs via the real tick runner (all 15 stages, tier system,
 * island coloring).  State is replicated to clients via repl_server
 * with a pose callback that reads body positions from the physics world.
 *
 * Usage:  ./demo_server <port> [duration_s]
 * Example: ./demo_server 40080 60
 */

#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ferrum/job/system.h"
#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"
#include "ferrum/net/rudp/peer.h"
#include "ferrum/net/topic_channel.h"
#include "ferrum/net/udp_socket.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/phys_cmd.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/phys_tick_runner.h"
#include "ferrum/physics/world.h"
#include "ferrum/server/repl_server.h"

/* ── Constants ──────────────────────────────────────────────────── */

#define DEMO_MAX_CLIENTS   4u
#define DEMO_MAX_ENTITIES  1024u
#define DEMO_TICK_HZ       60u
#define DEMO_SPAWN_INTERVAL_S  5.0
#define DEMO_SPAWN_MIN    20u
#define DEMO_SPAWN_MAX    50u
#define DEMO_SPAWN_Y_LO   20.0f
#define DEMO_SPAWN_Y_HI   40.0f
#define DEMO_SPAWN_AREA    10.0f
#define DEMO_BOX_HALF      0.5f
#define DEMO_BOX_MASS      1.0f
#define DEMO_GROUND_HALF_X 100.0f
#define DEMO_GROUND_HALF_Y 0.1f
#define DEMO_GROUND_HALF_Z 100.0f
#define DEMO_FIBER_STACK   (256u * 1024u)

/* ── Globals for signal handling ────────────────────────────────── */

static volatile sig_atomic_t g_running = 1;

static void handle_sigint(int sig) {
    (void)sig;
    g_running = 0;
}

/* ── Body → entity mapping ──────────────────────────────────────── */

/**
 * Maps physics body indices to repl_server entity indices.
 * Populated by the spawn callback.
 */
typedef struct body_entity_map {
    uint16_t *entity_index;   /**< body_index → entity_index (UINT16_MAX = unmapped). */
    uint32_t  capacity;
} body_entity_map_t;

static void body_entity_map_init(body_entity_map_t *m, uint16_t *storage, uint32_t cap) {
    m->entity_index = storage;
    m->capacity = cap;
    for (uint32_t i = 0; i < cap; ++i) {
        storage[i] = UINT16_MAX;
    }
}

/* ── Demo context ───────────────────────────────────────────────── */

typedef struct demo_ctx {
    /* Physics */
    phys_world_t          world;
    phys_job_context_t    phys_jobs;
    phys_tick_runner_t    tick_runner;
    fr_topic_channel_t   *cmd_channel;

    /* Replication */
    server_repl_server_t *repl;
    net_udp_socket_t      sock;

    /* Jobs */
    job_system_t          job_sys;

    /* Mapping */
    body_entity_map_t     map;
    uint32_t              next_entity_id;

    /* Timing */
    double                last_spawn_time;
    uint32_t              total_spawned;
} demo_ctx_t;

/* ── Pose callback ──────────────────────────────────────────────── */

/**
 * Called by repl_server to fetch an entity's current position and
 * rotation from the physics world.
 */
static bool demo_get_entity_pose(void *user,
                                 uint32_t entity_id,
                                 uint16_t entity_index,
                                 vec3_t *out_pos,
                                 quat_t *out_rot) {
    (void)entity_id;
    demo_ctx_t *ctx = (demo_ctx_t *)user;

    /* entity_index is used as body_index (1:1 mapping in this demo). */
    const phys_body_t *body = phys_world_get_body(&ctx->world, (uint32_t)entity_index);
    if (!body) {
        return false;
    }

    *out_pos = body->position;
    *out_rot = body->orientation;
    return true;
}

/* ── Spawn callback ─────────────────────────────────────────────── */

/**
 * Physics tick runner invokes this after creating a body.
 * We register the body as an entity in the repl_server.
 */
static void demo_spawn_callback(uint32_t body_index,
                                uint64_t user_tag,
                                void *user) {
    demo_ctx_t *ctx = (demo_ctx_t *)user;
    if (body_index == UINT32_MAX) {
        return;  /* spawn failed */
    }

    uint32_t entity_id = (uint32_t)user_tag;
    uint16_t entity_idx = 0;
    int rc = server_repl_server_debug_add_active_entity(
        ctx->repl, 0u /* owner=server */, entity_id, &entity_idx);
    if (rc != 0) {
        fprintf(stderr, "warn: failed to register entity %u for body %u\n",
                entity_id, body_index);
        return;
    }

    if (body_index < ctx->map.capacity) {
        ctx->map.entity_index[body_index] = entity_idx;
    }
}

/* ── Box rain spawner ───────────────────────────────────────────── */

static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13u;
    x ^= x >> 17u;
    x ^= x << 5u;
    *state = x;
    return x;
}

static float randf(uint32_t *rng, float lo, float hi) {
    return lo + (float)(xorshift32(rng) % 10000u) / 10000.0f * (hi - lo);
}

static void demo_spawn_box_rain(demo_ctx_t *ctx, uint32_t *rng) {
    uint32_t count = DEMO_SPAWN_MIN + (xorshift32(rng) % (DEMO_SPAWN_MAX - DEMO_SPAWN_MIN + 1u));

    /* Clamp to remaining entity budget. */
    if (ctx->total_spawned + count > DEMO_MAX_ENTITIES - 1u) {
        count = (DEMO_MAX_ENTITIES - 1u > ctx->total_spawned)
              ? (DEMO_MAX_ENTITIES - 1u - ctx->total_spawned) : 0u;
    }
    if (count == 0u) {
        return;
    }

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t eid = ctx->next_entity_id++;
        phys_cmd_spawn_body_t spawn = {
            .position = {
                randf(rng, -DEMO_SPAWN_AREA, DEMO_SPAWN_AREA),
                randf(rng, DEMO_SPAWN_Y_LO, DEMO_SPAWN_Y_HI),
                randf(rng, -DEMO_SPAWN_AREA, DEMO_SPAWN_AREA)
            },
            .orientation = {0.0f, 0.0f, 0.0f, 1.0f},
            .linear_vel  = {0.0f, 0.0f, 0.0f},
            .mass        = DEMO_BOX_MASS,
            .flags       = 0u,
            .shape       = PHYS_CMD_SHAPE_BOX,
            .shape_data.box_half = {DEMO_BOX_HALF, DEMO_BOX_HALF, DEMO_BOX_HALF},
            .user_tag    = (uint64_t)eid
        };

        if (!phys_cmd_push(ctx->cmd_channel, PHYS_CMD_SPAWN_BODY,
                           &spawn, sizeof(spawn))) {
            fprintf(stderr, "warn: cmd channel full, spawned %u of %u\n", i, count);
            break;
        }
    }

    ctx->total_spawned += count;
    printf("[server] spawned %u boxes (total: %u)\n", count, ctx->total_spawned);
}

/* ── Time helpers ───────────────────────────────────────────────── */

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port> [duration_s]\n", argv[0]);
        return 1;
    }
    uint16_t port = (uint16_t)atoi(argv[1]);
    double duration = (argc >= 3) ? atof(argv[2]) : 0.0;

    signal(SIGINT, handle_sigint);

    demo_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.next_entity_id = 1u;

    /* ── 1. Job system ─────────────────────────────────────────── */
    if (job_system_create(&ctx.job_sys, 4u, 256u, DEMO_FIBER_STACK, 64u, 0) != JOB_CREATE_OK) {
        fprintf(stderr, "error: job_system_create failed\n");
        return 1;
    }
    if (job_system_start(&ctx.job_sys) != 0) {
        fprintf(stderr, "error: job_system_start failed\n");
        return 1;
    }
    printf("[server] job system started (4 workers)\n");

    /* ── 2. Physics world ──────────────────────────────────────── */
    phys_world_config_t wcfg = phys_world_config_default();
    wcfg.max_bodies = DEMO_MAX_ENTITIES;
    wcfg.max_colliders = DEMO_MAX_ENTITIES;
    if (phys_world_init(&ctx.world, &wcfg) != 0) {
        fprintf(stderr, "error: phys_world_init failed\n");
        return 1;
    }
    printf("[server] physics world created (max %u bodies)\n", DEMO_MAX_ENTITIES);

    /* Ground plane: large static box at y=0. */
    {
        uint32_t gi = phys_world_create_body(&ctx.world);
        phys_body_t *gb = phys_world_get_body(&ctx.world, gi);
        gb->position = (phys_vec3_t){0.0f, 0.0f, 0.0f};
        gb->orientation = (phys_quat_t){0.0f, 0.0f, 0.0f, 1.0f};
        gb->flags |= PHYS_BODY_FLAG_STATIC;
        phys_world_set_box_collider(&ctx.world, gi,
            (phys_vec3_t){DEMO_GROUND_HALF_X, DEMO_GROUND_HALF_Y, DEMO_GROUND_HALF_Z},
            (phys_vec3_t){0.0f, 0.0f, 0.0f},
            (phys_quat_t){0.0f, 0.0f, 0.0f, 1.0f});
        printf("[server] ground plane body %u\n", gi);
    }

    /* ── 3. Physics command channel + tick runner ───────────────── */
    fr_topic_channel_config_t chan_cfg = {
        .capacity = 256u,
        .capacity_bytes = 256u * 1024u,
        .max_message_size = 1024u,
        .backpressure = FR_TOPIC_BACKPRESSURE_FAIL
    };
    ctx.cmd_channel = fr_topic_channel_create(&chan_cfg);
    if (!ctx.cmd_channel) {
        fprintf(stderr, "error: fr_topic_channel_create failed\n");
        return 1;
    }

    phys_job_context_init(&ctx.phys_jobs, &ctx.job_sys);
    phys_tick_runner_init(&ctx.tick_runner, &ctx.world, &ctx.phys_jobs,
                          ctx.cmd_channel, NULL,
                          demo_spawn_callback, &ctx);
    phys_tick_runner_start(&ctx.tick_runner);
    printf("[server] physics tick runner started\n");

    /* ── 4. Body → entity map (heap-allocated) ────────────────── */
    uint16_t *map_storage = (uint16_t *)calloc(DEMO_MAX_ENTITIES, sizeof(uint16_t));
    if (!map_storage) {
        fprintf(stderr, "error: map_storage alloc failed\n");
        return 1;
    }
    body_entity_map_init(&ctx.map, map_storage, DEMO_MAX_ENTITIES);

    /* ── 5. UDP socket ─────────────────────────────────────────── */
    if (net_udp_socket_open(&ctx.sock) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "error: net_udp_socket_open failed\n");
        return 1;
    }
    net_udp_addr_t bind_addr;
    net_udp_addr_ipv4(&bind_addr, 0u, 0u, 0u, 0u, port);
    if (net_udp_socket_bind(&ctx.sock, &bind_addr) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "error: bind to port %u failed\n", port);
        return 1;
    }
    net_udp_socket_set_nonblocking(&ctx.sock, 1);
    net_udp_socket_set_recv_buffer_bytes(&ctx.sock, 4u * 1024u * 1024u);
    net_udp_socket_set_send_buffer_bytes(&ctx.sock, 4u * 1024u * 1024u);
    printf("[server] listening on 0.0.0.0:%u\n", port);

    /* ── 6. Replication server ─────────────────────────────────── */
    size_t rudp_slots_per_client = (size_t)DEMO_MAX_ENTITIES + 8u;
    size_t total_rudp_slots = (size_t)DEMO_MAX_CLIENTS * rudp_slots_per_client;
    size_t rudp_bytes = net_rudp_send_slot_storage_size(total_rudp_slots);
    void *rudp_mem = calloc(1u, rudp_bytes);
    if (!rudp_mem) {
        fprintf(stderr, "error: rudp slot alloc failed\n");
        return 1;
    }

    server_repl_config_t rcfg = {
        .max_clients = DEMO_MAX_CLIENTS,
        .tick_hz     = DEMO_TICK_HZ,
        .max_entities = DEMO_MAX_ENTITIES,
        .resend_interval_ms = 50u,
        .get_entity_pose = demo_get_entity_pose,
        .get_entity_pose_user = &ctx,
        .rudp_send_slot_storage = rudp_mem,
        .rudp_send_slot_storage_bytes = rudp_bytes,
        .rudp_send_slots_per_client = rudp_slots_per_client
    };

    ctx.repl = server_repl_server_create(&rcfg, &ctx.sock, &ctx.job_sys);
    if (!ctx.repl) {
        fprintf(stderr, "error: server_repl_server_create failed\n");
        return 1;
    }
    printf("[server] replication server ready (max %u clients)\n", DEMO_MAX_CLIENTS);

    /* ── 7. Main loop ──────────────────────────────────────────── */
    uint32_t rng = 12345u;
    ctx.last_spawn_time = now_seconds();
    const double tick_period = 1.0 / (double)DEMO_TICK_HZ;
    double last_tick_time = now_seconds();
    double accumulator = 0.0;
    const double start_time = now_seconds();
    uint64_t tick_count = 0;

    printf("[server] entering main loop (Ctrl-C to stop)\n");

    while (g_running) {
        double now = now_seconds();
        double elapsed = now - last_tick_time;
        last_tick_time = now;
        accumulator += elapsed;

        /* Cap catch-up to prevent spiral of death. */
        if (accumulator > tick_period * 5.0) {
            accumulator = tick_period * 5.0;
        }

        /* Duration limit. */
        if (duration > 0.0 && (now - start_time) >= duration) {
            break;
        }

        /* Spawn box rain every DEMO_SPAWN_INTERVAL_S seconds. */
        if (now - ctx.last_spawn_time >= DEMO_SPAWN_INTERVAL_S) {
            demo_spawn_box_rain(&ctx, &rng);
            ctx.last_spawn_time = now;
        }

        /* Fixed-rate server ticks. */
        while (accumulator >= tick_period) {
            accumulator -= tick_period;

            /* Pump inbound network. */
            server_repl_server_pump(ctx.repl, now_ms());

            /* Replicate state to clients. */
            server_repl_server_tick(ctx.repl, now_ms());

            tick_count++;
        }

        /* Periodic stats. */
        if (tick_count > 0 && tick_count % (DEMO_TICK_HZ * 5u) == 0) {
            server_repl_stats_t st = server_repl_server_stats(ctx.repl);
            uint64_t phys_tick = phys_tick_runner_tick_id(&ctx.tick_runner);
            printf("[server] tick=%lu phys=%lu clients=%u entities=%u pkts_out=%lu\n",
                   (unsigned long)tick_count, (unsigned long)phys_tick,
                   st.clients_connected, ctx.total_spawned,
                   (unsigned long)st.packets_sent);
        }

        /* Yield to avoid busy spin. */
        {
            struct timespec ts = {0, 500000};  /* 0.5 ms */
            nanosleep(&ts, NULL);
        }
    }

    /* ── 8. Shutdown ───────────────────────────────────────────── */
    printf("\n[server] shutting down...\n");

    /* Signal the tick runner to stop.  The fiber checks this flag in
     * its pacing loop between ticks (NOT during a tick). */
    atomic_store_explicit(&ctx.tick_runner.stop_requested, 1, memory_order_release);

    /* Wait for the fiber to actually finish (it will set stopped=1
     * after its current tick completes and it re-checks stop_requested).
     * We MUST wait here; shutting down the job system while a physics
     * tick is in-flight causes use-after-free of job counters. */
    int waited_ms = 0;
    while (!atomic_load_explicit(&ctx.tick_runner.stopped, memory_order_acquire)) {
        struct timespec ts = {0, 1000000}; /* 1 ms */
        nanosleep(&ts, NULL);
        waited_ms++;
        if (waited_ms > 5000) {
            fprintf(stderr, "[server] warn: tick runner did not stop in 5s, forcing exit\n");
            break;
        }
    }
    ctx.tick_runner.running = 0;
    printf("[server] tick runner stopped after %d ms\n", waited_ms);

    /* Now safe to tear down: no fibers in flight. */
    job_system_shutdown(&ctx.job_sys);
    phys_tick_runner_destroy(&ctx.tick_runner);
    phys_job_context_destroy(&ctx.phys_jobs);
    phys_world_destroy(&ctx.world);
    fr_topic_channel_destroy(ctx.cmd_channel);
    server_repl_server_destroy(ctx.repl);
    net_udp_socket_close(&ctx.sock);
    free(rudp_mem);
    free(map_storage);

    printf("[server] done. %lu ticks, %u bodies spawned.\n",
           (unsigned long)tick_count, ctx.total_spawned);
    return 0;
}

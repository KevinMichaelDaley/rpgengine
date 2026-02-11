/**
 * @file demo_server.c
 * @brief Full-stack physics + replication demo server.
 *
 * Spawns a ground plane and periodically rains stacks of boxes.
 * Physics runs via the real tick runner (all 15 stages, tier system,
 * island coloring).  State is replicated to clients via the new
 * server net runtime (fr_server_net_runtime), entity net pump,
 * tick loop, and body state broadcaster.
 *
 * Usage:  ./demo_server <port> [duration_s]
 * Example: ./demo_server 40080 60
 */

#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ferrum/job/system.h"
#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"
#include "ferrum/net/quantization.h"
#include "ferrum/net/replication/body_spawn.h"
#include "ferrum/net/replication/body_state.h"
#include "ferrum/net/replication/common.h"
#include "ferrum/net/topic_channel.h"
#include "ferrum/net/udp_socket.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/phys_cmd.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/phys_tick_runner.h"
#include "ferrum/physics/game_state.h"
#include "ferrum/physics/world.h"
#include "ferrum/server/entity/net/pump.h"
#include "ferrum/server/net/inbound_message.h"
#include "ferrum/server/net/runtime.h"
#include "ferrum/server/physics/net/body_state_broadcast.h"
#include "ferrum/server/tick_loop.h"

/* ── Constants ──────────────────────────────────────────────────── */

#define DEMO_MAX_CLIENTS       4u
#define DEMO_MAX_BODIES        1024u
#define DEMO_TICK_HZ           30u
#define DEMO_SPAWN_INTERVAL_S  10.0
#define DEMO_SPAWN_MIN         20u
#define DEMO_SPAWN_MAX         50u
#define DEMO_SPAWN_Y_LO        20.0f
#define DEMO_SPAWN_Y_HI        30.0f
#define DEMO_SPAWN_AREA        100.0f
#define DEMO_BOX_HALF          0.5f
#define DEMO_BOX_MASS          1.0f
#define DEMO_GROUND_HALF_X     1000.0f
#define DEMO_GROUND_HALF_Y     0.1f
#define DEMO_GROUND_HALF_Z     1000.0f
#define DEMO_FIBER_STACK       (256u * 1024u)

/* ── Globals for signal handling ────────────────────────────────── */

static volatile sig_atomic_t g_running = 1;

static void handle_sigint(int sig) {
    (void)sig;
    g_running = 0;
}

/* ── Demo context (forward declaration) ─────────────────────────── */

typedef struct demo_ctx demo_ctx_t;

struct demo_ctx {
    /* Physics */
    phys_world_t                        world;
    phys_job_context_t                  phys_jobs;
    phys_tick_runner_t                  tick_runner;
    fr_topic_channel_t                 *cmd_channel;

    /* Networking: new runtime */
    net_udp_socket_t                    sock;
    fr_server_net_runtime_t            *net_rt;
    fr_server_entity_net_pump_t        *entity_pump;
    fr_server_body_state_broadcast_t   *broadcaster;

    /* Topic channels */
    fr_topic_channel_t                 *inbound_topic;
    fr_topic_channel_t                 *player_event_topic;
    fr_topic_channel_t                 *entity_event_topic;

    /* Tick loop */
    fr_server_tick_loop_t               tick_loop;

    /* Jobs */
    job_system_t                        job_sys;      /**< Networking fibers. */
    job_system_t                        phys_job_sys; /**< Physics parallel stages. */

    /* Tier classification */
    phys_game_state_t                   game_state;   /**< Player position for tiers. */

    /* Client tracking */
    bool                                client_joined[DEMO_MAX_CLIENTS];
    uint32_t                            clients_connected;

    /* Spawned body tracking: which bodies have been announced to clients.
     * spawned_to_client[client_id * DEMO_MAX_BODIES + body_index] = 1
     * when BODY_SPAWN has been sent for that body to that client. */
    uint8_t                            *spawned_to_client;

    /* Timing / spawn */
    double                              last_spawn_time;
    uint32_t                            total_spawned;
    uint32_t                            server_tick;
};

/* ── Helpers ────────────────────────────────────────────────────── */

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

/* ── Topic-out callback (for entity pump + broadcaster) ─────────── */

/**
 * Callback shared by entity pump and body state broadcaster to obtain
 * per-client outbound topic channels from the net runtime.
 */
static bool get_client_out_topics_cb(void *user,
                                     uint16_t client_id,
                                     fr_topic_channel_t **out_reliable,
                                     fr_topic_channel_t **out_unreliable) {
    demo_ctx_t *ctx = (demo_ctx_t *)user;
    return fr_server_net_runtime_client_out_topics(
        ctx->net_rt, client_id, out_reliable, out_unreliable);
}

/* Maximum spawn messages to send per client per drain tick.
 * This rate-limits the reliable pipe to prevent overwhelming the
 * RUDP send window during large spawn bursts. */
#define DEMO_SPAWN_SEND_RATE 8u

/* ── Send BODY_SPAWN for all known bodies to a newly-joined client ── */

static void send_body_spawns_to_client(demo_ctx_t *ctx, uint16_t client_id) {
    fr_topic_channel_t *reliable = NULL;
    fr_topic_channel_t *unreliable = NULL;
    if (!fr_server_net_runtime_client_out_topics(
            ctx->net_rt, client_id, &reliable, &unreliable)) {
        return;
    }

    uint32_t body_count = phys_world_body_count(&ctx->world);
    uint32_t sent_this_tick = 0u;
    for (uint32_t bi = 0; bi < body_count; ++bi) {
        if (sent_this_tick >= DEMO_SPAWN_SEND_RATE) {
            break; /* Rate limit — remaining bodies picked up next drain. */
        }
        const phys_body_t *body = phys_world_get_body(&ctx->world, bi);
        if (!body) {
            continue;
        }

        size_t idx = (size_t)client_id * DEMO_MAX_BODIES + bi;
        if (ctx->spawned_to_client[idx]) {
            continue;
        }

        /* Build and encode BODY_SPAWN message. */
        net_repl_body_spawn_t spawn_msg;
        memset(&spawn_msg, 0, sizeof(spawn_msg));
        spawn_msg.body_id = (uint16_t)bi;
        spawn_msg.flags = (body->flags & PHYS_BODY_FLAG_STATIC) ? 1u : 0u;
        spawn_msg.shape_type = 0u; /* box */
        spawn_msg.color_seed = bi;

        net_qvec3_mm_t qpos;
        net_quantize_vec3_mm(
            (vec3_t){body->position.x, body->position.y, body->position.z},
            &qpos);
        spawn_msg.pos_mm = (net_repl_vec3_mm_t){qpos.x_mm, qpos.y_mm, qpos.z_mm};

        spawn_msg.rot_x = body->orientation.x;
        spawn_msg.rot_y = body->orientation.y;
        spawn_msg.rot_z = body->orientation.z;
        spawn_msg.rot_w = body->orientation.w;

        /* Encode half-extents as float16 (meters).  float16 can
         * represent up to 65504 with ~3 sig digits — plenty for
         * both 0.5 m boxes and 1000 m ground planes. */
        if (body->flags & PHYS_BODY_FLAG_STATIC) {
            spawn_msg.half_x_f16 = net_float16_from_float(DEMO_GROUND_HALF_X);
            spawn_msg.half_y_f16 = net_float16_from_float(DEMO_GROUND_HALF_Y);
            spawn_msg.half_z_f16 = net_float16_from_float(DEMO_GROUND_HALF_Z);
        } else {
            spawn_msg.half_x_f16 = net_float16_from_float(DEMO_BOX_HALF);
            spawn_msg.half_y_f16 = net_float16_from_float(DEMO_BOX_HALF);
            spawn_msg.half_z_f16 = net_float16_from_float(DEMO_BOX_HALF);
        }

        uint8_t wire[2u + NET_REPL_BODY_SPAWN_PAYLOAD_SIZE];
        wire[0] = (uint8_t)(NET_REPL_SCHEMA_BODY_SPAWN & 0xFFu);
        wire[1] = (uint8_t)((NET_REPL_SCHEMA_BODY_SPAWN >> 8u) & 0xFFu);
        if (net_repl_body_spawn_encode(&spawn_msg, wire + 2u,
                                       NET_REPL_BODY_SPAWN_PAYLOAD_SIZE) == NET_REPL_OK) {
            if (fr_topic_channel_push(reliable, wire, sizeof(wire))) {
                ctx->spawned_to_client[idx] = 1u;
                sent_this_tick++;
            } else {
                fprintf(stderr, "[server] WARN: reliable topic full for client %u body %u\n",
                        client_id, bi);
            }
        }
    }
}

/* ── Physics spawn callback ─────────────────────────────────────── */

/**
 * Physics tick runner invokes this after creating a body via phys_cmd.
 * We don't register with repl_server anymore; instead body spawns are
 * sent to clients in the drain callback when we detect new bodies.
 */
static void demo_spawn_callback(uint32_t body_index,
                                uint64_t user_tag,
                                void *user) {
    (void)user_tag;
    (void)user;
    if (body_index == UINT32_MAX) {
        return; /* spawn failed */
    }
    /* Body is now in the world; send_body_spawns_to_client will pick it up. */
}

/* ── Box rain spawner ───────────────────────────────────────────── */

static void demo_spawn_box_rain(demo_ctx_t *ctx, uint32_t *rng) {
    uint32_t count = DEMO_SPAWN_MIN +
        (xorshift32(rng) % (DEMO_SPAWN_MAX - DEMO_SPAWN_MIN + 1u));

    /* Clamp to remaining entity budget. */
    if (ctx->total_spawned + count > DEMO_MAX_BODIES - 1u) {
        count = (DEMO_MAX_BODIES - 1u > ctx->total_spawned)
              ? (DEMO_MAX_BODIES - 1u - ctx->total_spawned) : 0u;
    }
    if (count == 0u) {
        return;
    }

    /* Spawn boxes in vertical stacks (columns).
     * Pick 1-2 random XZ positions, then stack boxes upward from SPAWN_Y_LO. */
    uint32_t num_stacks = 1u + (xorshift32(rng) % 2u); /* 1 or 2 stacks */
    float stack_x[2];
    float stack_z[2];
    for (uint32_t s = 0; s < num_stacks; ++s) {
        stack_x[s] = randf(rng, -DEMO_SPAWN_AREA, DEMO_SPAWN_AREA);
        stack_z[s] = randf(rng, -DEMO_SPAWN_AREA, DEMO_SPAWN_AREA);
    }

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t stack_idx = i % num_stacks;
        uint32_t layer = i / num_stacks;
        float y = DEMO_BOX_HALF + (float)layer * (DEMO_BOX_HALF * 2.0f + 0.01f)
                  + DEMO_SPAWN_Y_LO;

        phys_cmd_spawn_body_t spawn = {
            .position = {
                stack_x[stack_idx],
                y,
                stack_z[stack_idx]
            },
            .orientation = {0.0f, 0.0f, 0.0f, 1.0f},
            .linear_vel  = {0.0f, 0.0f, 0.0f},
            .mass        = DEMO_BOX_MASS,
            .flags       = 0u,
            .shape       = PHYS_CMD_SHAPE_BOX,
            .shape_data.box_half = {DEMO_BOX_HALF, DEMO_BOX_HALF, DEMO_BOX_HALF},
            .user_tag    = 0u
        };

        if (!phys_cmd_push(ctx->cmd_channel, PHYS_CMD_SPAWN_BODY,
                           &spawn, sizeof(spawn))) {
            fprintf(stderr, "warn: cmd channel full, spawned %u of %u\n", i, count);
            break;
        }
    }

    ctx->total_spawned += count;
    printf("[server] spawned %u boxes in %u stacks (total: %u)\n",
           count, num_stacks, ctx->total_spawned);
}

/* ── Tick loop callbacks ────────────────────────────────────────── */

/**
 * Stage 1: drain inbound messages.
 * Pumps the net runtime and entity pump, then processes player/entity events.
 */
static void on_drain(void *user) {
    demo_ctx_t *ctx = (demo_ctx_t *)user;
    uint64_t now = now_ms();

    /* Pump UDP receive and per-client fiber processing. */
    fr_server_net_runtime_pump(ctx->net_rt, now);

    /* Route decoded inbound messages to player/entity event topics. */
    fr_server_entity_net_pump_tick(ctx->entity_pump, now);

    /* Drain player events (JOINs). */
    for (;;) {
        uint8_t evt[64];
        size_t evt_len = sizeof(evt);
        if (!fr_topic_channel_pop(ctx->player_event_topic, evt, &evt_len)) {
            break;
        }
        if (evt[0] == FR_SERVER_EVT_PLAYER_JOIN && evt_len >= 4u) {
            uint16_t client_id = (uint16_t)evt[2] | ((uint16_t)evt[3] << 8u);
            if (client_id < DEMO_MAX_CLIENTS && !ctx->client_joined[client_id]) {
                ctx->client_joined[client_id] = true;
                ctx->clients_connected++;
                printf("[server] client %u joined (total: %u)\n",
                       client_id, ctx->clients_connected);

                /* Tell entity pump to spawn this player to remote clients. */
                fr_server_entity_net_pump_set_player_should_spawn_remote(
                    ctx->entity_pump, client_id, true);

                /* Send all existing body spawns to the new client. */
                send_body_spawns_to_client(ctx, client_id);
            }
        }
    }

    /* Send any new body spawns to already-connected clients. */
    for (uint16_t ci = 0; ci < DEMO_MAX_CLIENTS; ++ci) {
        if (ctx->client_joined[ci]) {
            send_body_spawns_to_client(ctx, ci);
        }
    }

    /* Drain entity events (INPUT_MOVE).
     * Format: [evt_type:u8][reserved:u8][client_id:u16 LE][payload...]
     * INPUT_MOVE payload: [tick:u32 LE][move_x:f32 LE][move_y:f32 LE][move_z:f32 LE]
     */
    for (;;) {
        uint8_t evt[256];
        size_t evt_len = sizeof(evt);
        if (!fr_topic_channel_pop(ctx->entity_event_topic, evt, &evt_len)) {
            break;
        }
        /* For now, we acknowledge but don't process move inputs.
         * The physics world is server-authoritative for box rain;
         * future: apply kinematic intent from INPUT_MOVE to player bodies. */
        (void)evt;
    }
}

/**
 * Stage 2: physics (no-op—tick runner is async fiber).
 */
static void on_physics(void *user) {
    (void)user;
}

/**
 * Stage 3: encode replication (body state broadcast).
 */
static void on_encode(void *user) {
    demo_ctx_t *ctx = (demo_ctx_t *)user;
    ctx->server_tick++;
    fr_server_body_state_broadcast_tick(
        ctx->broadcaster, (uint16_t)ctx->server_tick, now_ms());
}

/**
 * Stage 4: flush (no-op—net runtime fibers handle outbound).
 */
static void on_flush(void *user) {
    (void)user;
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

    /* ── 1. Job systems ───────────────────────────────────────────── */
    /* Networking job system: client fibers, demux pump, entity pump. */
    if (job_system_create(&ctx.job_sys, 2u, 256u, DEMO_FIBER_STACK,
                          256u, 0) != JOB_CREATE_OK) {
        fprintf(stderr, "error: job_system_create (net) failed\n");
        return 1;
    }
    if (job_system_start(&ctx.job_sys) != 0) {
        fprintf(stderr, "error: job_system_start (net) failed\n");
        return 1;
    }
    /* Physics job system: parallel stages (broadphase, narrow, solve, etc). */
    if (job_system_create(&ctx.phys_job_sys, 6u, 4096u, DEMO_FIBER_STACK,
                          4096u, 0) != JOB_CREATE_OK) {
        fprintf(stderr, "error: job_system_create (phys) failed\n");
        return 1;
    }
    if (job_system_start(&ctx.phys_job_sys) != 0) {
        fprintf(stderr, "error: job_system_start (phys) failed\n");
        return 1;
    }
    printf("[server] job systems started (net=2 workers, phys=6 workers)\n");

    /* ── 2. Physics world ──────────────────────────────────────── */
    phys_world_config_t wcfg = phys_world_config_default();
    wcfg.max_bodies = DEMO_MAX_BODIES;
    wcfg.max_colliders = DEMO_MAX_BODIES;
    if (phys_world_init(&ctx.world, &wcfg) != 0) {
        fprintf(stderr, "error: phys_world_init failed\n");
        return 1;
    }
    printf("[server] physics world created (max %u bodies)\n", DEMO_MAX_BODIES);

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

    phys_job_context_init(&ctx.phys_jobs, &ctx.phys_job_sys);
    phys_tick_runner_init(&ctx.tick_runner, &ctx.world, &ctx.phys_jobs,
                          ctx.cmd_channel, NULL,
                          demo_spawn_callback, &ctx);

    /* Set up game state for tier classification.
     * Place a "player" at the origin so bodies near the spawn area get
     * high-fidelity tiers while distant/sleeping ones drop to T4/T5. */
    phys_game_state_init(&ctx.game_state);
    phys_player_state_t player0 = {
        .position = {0.0f, 0.0f, 0.0f},
        .interaction_radius = 5.0f,
    };
    phys_game_state_set_player(&ctx.game_state, 0, &player0);
    ctx.tick_runner.game_state = &ctx.game_state;

    phys_tick_runner_start(&ctx.tick_runner);
    printf("[server] physics tick runner started\n");

    /* ── 4. UDP socket ─────────────────────────────────────────── */
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

    /* ── 5. Topic channels ─────────────────────────────────────── */
    fr_topic_channel_config_t tc_cfg = {
        .capacity = 4096u,
        .capacity_bytes = 256u * 1024u,
        .max_message_size = 1472u,
        .backpressure = FR_TOPIC_BACKPRESSURE_DROP_OLDEST
    };
    ctx.inbound_topic = fr_topic_channel_create(&tc_cfg);
    ctx.player_event_topic = fr_topic_channel_create(&tc_cfg);
    ctx.entity_event_topic = fr_topic_channel_create(&tc_cfg);
    if (!ctx.inbound_topic || !ctx.player_event_topic || !ctx.entity_event_topic) {
        fprintf(stderr, "error: topic channel creation failed\n");
        return 1;
    }

    /* ── 6. Server net runtime ─────────────────────────────────── */
    fr_server_net_runtime_config_t rt_cfg = {
        .max_clients = DEMO_MAX_CLIENTS,
        .jobs = &ctx.job_sys,
        .socket = &ctx.sock,
        .inbound_topic = ctx.inbound_topic,
        .out_reliable_capacity = 8192u,
        .out_unreliable_capacity = 8192u,
    };
    ctx.net_rt = fr_server_net_runtime_create(&rt_cfg);
    if (!ctx.net_rt) {
        fprintf(stderr, "error: fr_server_net_runtime_create failed\n");
        return 1;
    }
    printf("[server] net runtime created (max %u clients)\n", DEMO_MAX_CLIENTS);

    /* ── 7. Entity net pump ────────────────────────────────────── */
    fr_server_entity_net_pump_config_t pump_cfg = {
        .max_clients = DEMO_MAX_CLIENTS,
        .tick_hz = DEMO_TICK_HZ,
        .expected_entities = (uint16_t)DEMO_MAX_BODIES,
        .inbound_topic = ctx.inbound_topic,
        .player_event_topic = ctx.player_event_topic,
        .entity_event_topic = ctx.entity_event_topic,
        .get_client_out_topics_cb = get_client_out_topics_cb,
        .io_user = &ctx,
    };
    ctx.entity_pump = fr_server_entity_net_pump_create(&pump_cfg);
    if (!ctx.entity_pump) {
        fprintf(stderr, "error: fr_server_entity_net_pump_create failed\n");
        return 1;
    }
    printf("[server] entity net pump created\n");

    /* ── 8. Body state broadcaster ─────────────────────────────── */
    fr_server_body_state_broadcast_config_t bc_cfg = {
        .max_clients = DEMO_MAX_CLIENTS,
        .world = &ctx.world,
        .get_client_out_topics_cb = get_client_out_topics_cb,
        .io_user = &ctx,
    };
    ctx.broadcaster = fr_server_body_state_broadcast_create(&bc_cfg);
    if (!ctx.broadcaster) {
        fprintf(stderr, "error: fr_server_body_state_broadcast_create failed\n");
        return 1;
    }
    printf("[server] body state broadcaster created\n");

    /* ── 9. Spawned-to-client tracking ─────────────────────────── */
    ctx.spawned_to_client = (uint8_t *)calloc(
        (size_t)DEMO_MAX_CLIENTS * (size_t)DEMO_MAX_BODIES, 1u);
    if (!ctx.spawned_to_client) {
        fprintf(stderr, "error: spawned_to_client alloc failed\n");
        return 1;
    }

    /* ── 10. Tick loop ─────────────────────────────────────────── */
    fr_server_tick_loop_config_t tl_cfg = {
        .tick_hz = DEMO_TICK_HZ,
        .max_catchup_ticks = 5u,
        .on_drain = on_drain,
        .on_physics = on_physics,
        .on_encode = on_encode,
        .on_flush = on_flush,
        .user = &ctx,
    };
    if (fr_server_tick_loop_init(&ctx.tick_loop, &tl_cfg) != 0) {
        fprintf(stderr, "error: fr_server_tick_loop_init failed\n");
        return 1;
    }

    /* ── 11. Main loop ─────────────────────────────────────────── */
    uint32_t rng = 12345u;
    ctx.last_spawn_time = 0.0;  /* Force immediate first spawn. */
    const double start_time = now_seconds();
    double last_step_time = now_seconds();

    printf("[server] entering main loop (Ctrl-C to stop)\n");

    while (g_running) {
        double now = now_seconds();
        uint64_t elapsed_us = (uint64_t)((now - last_step_time) * 1e6);
        last_step_time = now;

        /* Duration limit. */
        if (duration > 0.0 && (now - start_time) >= duration) {
            break;
        }

        /* Spawn box rain every DEMO_SPAWN_INTERVAL_S seconds. */
        if (now - ctx.last_spawn_time >= DEMO_SPAWN_INTERVAL_S) {
            demo_spawn_box_rain(&ctx, &rng);
            ctx.last_spawn_time = now;
        }

        /* Fixed-rate server ticks via tick loop. */
        fr_server_tick_loop_step(&ctx.tick_loop, elapsed_us);

        /* Periodic stats. */
        uint64_t tick_id = fr_server_tick_loop_tick_id(&ctx.tick_loop);
        if (tick_id > 0 && tick_id % (DEMO_TICK_HZ * 5u) == 0) {
            uint64_t phys_tick = phys_tick_runner_tick_id(&ctx.tick_runner);
            uint64_t tick_ns = phys_tick_runner_last_tick_ns(&ctx.tick_runner);
            printf("[server] tick=%lu phys=%lu clients=%u entities=%u tick_us=%lu\n",
                   (unsigned long)tick_id, (unsigned long)phys_tick,
                   ctx.clients_connected, ctx.total_spawned,
                   (unsigned long)(tick_ns / 1000u));
        }

        /* Yield to avoid busy spin. */
        struct timespec ts = {0, 500000}; /* 0.5 ms */
        nanosleep(&ts, NULL);
    }

    /* ── 12. Shutdown ──────────────────────────────────────────── */
    printf("\n[server] shutting down...\n");

    /* Stop physics tick runner thread (joins the dedicated thread). */
    phys_tick_runner_stop(&ctx.tick_runner);
    printf("[server] tick runner stopped\n");

    /* Tear down in reverse init order. */
    fr_server_body_state_broadcast_destroy(ctx.broadcaster);
    fr_server_entity_net_pump_destroy(ctx.entity_pump);
    fr_server_net_runtime_destroy(ctx.net_rt);
    job_system_shutdown(&ctx.job_sys);
    phys_tick_runner_destroy(&ctx.tick_runner);
    phys_job_context_destroy(&ctx.phys_jobs);
    job_system_shutdown(&ctx.phys_job_sys);
    phys_world_destroy(&ctx.world);
    fr_topic_channel_destroy(ctx.cmd_channel);
    fr_topic_channel_destroy(ctx.inbound_topic);
    fr_topic_channel_destroy(ctx.player_event_topic);
    fr_topic_channel_destroy(ctx.entity_event_topic);
    net_udp_socket_close(&ctx.sock);
    free(ctx.spawned_to_client);

    uint64_t final_tick = fr_server_tick_loop_tick_id(&ctx.tick_loop);
    printf("[server] done. %lu ticks, %u bodies spawned.\n",
           (unsigned long)final_tick, ctx.total_spawned);
    return 0;
}

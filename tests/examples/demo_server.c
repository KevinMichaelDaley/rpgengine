/**
 * @file demo_server.c
 * @brief Standalone demo server — empty scene with editor integration.
 *
 * Starts an empty physics world with networking and an editor TCP socket.
 * All entities are created via the editor protocol (spawn command).
 *
 * Usage:  ./demo_server <port> [duration_s] [--edit-port PORT]
 * Example: ./demo_server 40080 0 --edit-port 9100
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
#include "ferrum/net/replication/mesh_data.h"
#include "ferrum/net/topic_channel.h"
#include "ferrum/net/udp_socket.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/joint.h"
#include "ferrum/physics/phys_cmd.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/phys_tick_runner.h"
#include "ferrum/physics/game_state.h"
#include "ferrum/physics/world.h"
#include "ferrum/server/entity/net/pump.h"
#include "ferrum/server/net/inbound_message.h"
#include "ferrum/server/net/runtime.h"
#include "ferrum/server/physics/net/priority_body_sender.h"
#include "ferrum/server/tick_loop.h"
#include "ferrum/physics/snapshot.h"
#include "ferrum/net/snapshot_chunk.h"

#include "ferrum/editor/editor_ctx.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_physics_ctrl.h"
#include "ferrum/editor/edit_script_env.h"
#include "ferrum/physics/phys_overlap.h"

#include "ferrum/aegis/aegis_runtime.h"
#include "ferrum/aegis/aegis_async.h"
#include "ferrum/aegis/aegis_async_execute.h"
#include "ferrum/aegis/aegis_event.h"

#ifdef FR_NET_EMULATION
#include "ferrum/engine_settings.h"
#include "ferrum/net/emulation/net_emulator.h"
#endif

/* ── Constants ──────────────────────────────────────────────────── */

#define DEMO_MAX_CLIENTS       4u
#define DEMO_MAX_BODIES        1024u
#define DEMO_TICK_HZ           30u
#define DEMO_BOX_HALF          0.5f
#define DEMO_BOX_MASS          1.0f
#define DEMO_FIBER_STACK       (256u * 1024u)

/** Maximum snapshot wire size: header(12) + 1024 bodies × 26 bytes. */
#define DEMO_SNAPSHOT_BUF_SIZE (12u + DEMO_MAX_BODIES * 26u)

/** Chunk size for snapshot splitting (fits in one UDP datagram). */
#define DEMO_SNAPSHOT_CHUNK_SIZE 1200u

/** Max chunks per snapshot (ceil(DEMO_SNAPSHOT_BUF_SIZE / CHUNK_SIZE)). */
#define DEMO_SNAPSHOT_MAX_CHUNKS 24u

/** Maximum entity snapshots for script entity view. */
#define DEMO_SCRIPT_MAX_ENTITIES 256u

/** Async task buffer capacity (must be power of 2). */
#define DEMO_ASYNC_TASK_CAP 64u

/** Chunk header wire size: schema(2) + chunk_idx(2) + chunk_total(2)
 *  + offset(4) + length(4) = 14 bytes. */
#define DEMO_CHUNK_HDR_WIRE 14u

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

    /* Timing */
    uint64_t                            last_stats_tick;
    uint32_t                            total_spawned;
    uint32_t                            server_tick;

    /* Per-body shape type (0=box, 1=sphere, 2=capsule, 3=mesh). */
    uint8_t                             body_shape_type[DEMO_MAX_BODIES];

    /* Per-body half-extents for network spawn messages. */
    float                               body_half[DEMO_MAX_BODIES][3];

    /* Per-body packed RGB color (R8G8B8 in bits 23..0). */
    uint32_t                            body_color[DEMO_MAX_BODIES];

    /* Per-body hidden flag (trigger volumes are invisible). */
    uint8_t                             body_hidden[DEMO_MAX_BODIES];

    /* Per-body FVMA mesh data (NULL for non-mesh bodies). */
    uint8_t                            *body_fvma[DEMO_MAX_BODIES];
    uint32_t                            body_fvma_size[DEMO_MAX_BODIES];

    /* Mesh data sent tracking: same layout as spawned_to_client.
     * mesh_sent_to_client[client_id * DEMO_MAX_BODIES + body_index] = 1
     * when MESH_DATA has been fully sent for that body to that client. */
    uint8_t                            *mesh_sent_to_client;

    /** Per-body flag: 1 if body participates in at least one joint.
     *  Clients use this to choose interpolation (constrained) vs prediction. */
    uint8_t                             body_constrained[DEMO_MAX_BODIES];

    /** Joint pair array: [a0,b0,a1,b1,...] for priority sender. */
    uint32_t                            joint_pairs[DEMO_MAX_BODIES * 2];
    uint32_t                            joint_pair_count;

    /* Pre-allocated snapshot encoding buffer. */
    uint8_t                             snap_buf[DEMO_SNAPSHOT_BUF_SIZE];

    /* Priority body state sender (velocity-proportional rate). */
    fr_priority_body_sender_t          *priority_sender;

    /* Editor integration */
    editor_ctx_t                        editor;
    edit_physics_bridge_t               editor_bridge;
    edit_physics_ctrl_t                 physics_ctrl;
    uint16_t                            edit_port;

    /* Scripting */
    aegis_script_runtime_t              script_runtime;
    aegis_async_buffer_t                script_async_buf;
    script_entity_snapshot_t            script_snapshots[DEMO_SCRIPT_MAX_ENTITIES];
    script_entity_view_t                script_entity_view;

    /* Cached client addresses for raw UDP sends from physics thread. */
    net_udp_addr_t                      client_addrs[DEMO_MAX_CLIENTS];
    uint8_t                             client_addr_active[DEMO_MAX_CLIENTS];
};

/* ── Helpers ────────────────────────────────────────────────────── */

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ── Editor-to-physics bridge callbacks ────────────────────────── */

/**
 * @brief Bridge: create a physics body when editor spawns an entity.
 */
static uint32_t bridge_on_spawn_(void *user_data, uint32_t entity_id,
                                  const edit_entity_t *entity) {
    demo_ctx_t *ctx = (demo_ctx_t *)user_data;
    (void)entity_id;

    phys_cmd_spawn_body_t spawn;
    memset(&spawn, 0, sizeof(spawn));
    spawn.position = (phys_vec3_t){entity->pos[0], entity->pos[1], entity->pos[2]};
    spawn.orientation = (phys_quat_t){0.0f, 0.0f, 0.0f, 1.0f};

    /* Determine shape from entity type. */
    if (entity->type == EDIT_ENTITY_TYPE_SPHERE) {
        spawn.shape = PHYS_CMD_SHAPE_SPHERE;
        spawn.shape_data.sphere_r = entity->scale[0] * 0.5f;
    } else if (entity->type == EDIT_ENTITY_TYPE_CAPSULE) {
        spawn.shape = PHYS_CMD_SHAPE_CAPSULE;
        spawn.shape_data.capsule.radius      = entity->scale[0] * 0.5f;
        spawn.shape_data.capsule.half_height  = entity->scale[1] * 0.5f;
    } else {
        spawn.shape = PHYS_CMD_SHAPE_BOX;
        spawn.shape_data.box_half = (phys_vec3_t){
            entity->scale[0] * DEMO_BOX_HALF,
            entity->scale[1] * DEMO_BOX_HALF,
            entity->scale[2] * DEMO_BOX_HALF,
        };
    }

    /* Read mass from attrs (default: DEMO_BOX_MASS). */
    spawn.mass = DEMO_BOX_MASS;
    {
        uint8_t at = 0, as = 0;
        const void *mv = entity_attrs_get(&entity->attrs, SCRIPT_KEY_MASS,
                                          &at, &as);
        if (mv && as == 4) {
            if (at == SCRIPT_ATTR_F32) {
                memcpy(&spawn.mass, mv, 4);
            } else if (at == SCRIPT_ATTR_I32) {
                int32_t iv;
                memcpy(&iv, mv, 4);
                spawn.mass = (float)iv;
            }
        }
    }

    /* Read static flag from attrs. */
    {
        uint8_t at = 0, as = 0;
        const void *sv = entity_attrs_get(&entity->attrs, SCRIPT_KEY_STATIC,
                                          &at, &as);
        if (sv && as == 1 && *(const uint8_t *)sv) {
            spawn.flags |= PHYS_BODY_FLAG_STATIC;
            spawn.mass = 0.0f;
        }
    }

    /* Check if entity has the trigger attribute (key 256 = SCRIPT_KEY_USER).
     * Trigger bodies are static, invisible, and have the TRIGGER flag. */
    bool is_trigger = false;
    {
        uint8_t attr_type = 0, attr_size = 0;
        const void *val = entity_attrs_get(&entity->attrs, SCRIPT_KEY_USER,
                                           &attr_type, &attr_size);
        if (val && attr_size == 1 && *(const uint8_t *)val) {
            is_trigger = true;
            spawn.mass  = 0.0f;
            spawn.flags = PHYS_BODY_FLAG_STATIC | PHYS_BODY_FLAG_TRIGGER;
        }
    }

    phys_cmd_push(ctx->cmd_channel, PHYS_CMD_SPAWN_BODY,
                  &spawn, sizeof(spawn));

    /* Store shape type, half-extents, and color for network spawn messages. */
    uint32_t bi = ctx->total_spawned;
    if (bi < DEMO_MAX_BODIES) {
        if (entity->type == EDIT_ENTITY_TYPE_SPHERE) {
            ctx->body_shape_type[bi] = 1; /* sphere */
            float r = spawn.shape_data.sphere_r;
            ctx->body_half[bi][0] = r;
            ctx->body_half[bi][1] = r;
            ctx->body_half[bi][2] = r;
        } else if (entity->type == EDIT_ENTITY_TYPE_CAPSULE) {
            ctx->body_shape_type[bi] = 2; /* capsule */
            ctx->body_half[bi][0] = spawn.shape_data.capsule.radius;
            ctx->body_half[bi][1] = spawn.shape_data.capsule.half_height;
            ctx->body_half[bi][2] = spawn.shape_data.capsule.radius;
        } else {
            ctx->body_shape_type[bi] = 0; /* box */
            ctx->body_half[bi][0] = spawn.shape_data.box_half.x;
            ctx->body_half[bi][1] = spawn.shape_data.box_half.y;
            ctx->body_half[bi][2] = spawn.shape_data.box_half.z;
        }

        /* Generate a distinguishable color from entity_id hash.
         * Pastel palette: clamp channels to [80..220] for visibility. */
        uint32_t h = entity_id * 2654435761u;
        uint8_t cr = (uint8_t)(80u + (h & 0x7Fu));          /* 80..207 */
        uint8_t cg = (uint8_t)(80u + ((h >> 8u) & 0x7Fu));
        uint8_t cb = (uint8_t)(80u + ((h >> 16u) & 0x7Fu));
        ctx->body_color[bi] = ((uint32_t)cr << 16u) | ((uint32_t)cg << 8u) | cb;
        ctx->body_hidden[bi] = is_trigger ? 1u : 0u;
    }
    ctx->total_spawned++;

    /* Body index assigned by physics engine on next tick; we don't have it
     * synchronously. Return total_spawned as a tracking hint. */
    return ctx->total_spawned - 1;
}

/**
 * @brief Bridge: store FVMA mesh data for a body.
 *
 * Copies the FVMA blob so it can be sent to clients later.
 */
static void bridge_on_mesh_data_(void *user_data, uint32_t body_index,
                                  const uint8_t *fvma_data,
                                  uint32_t fvma_size) {
    demo_ctx_t *ctx = (demo_ctx_t *)user_data;
    if (body_index >= DEMO_MAX_BODIES || !fvma_data || fvma_size == 0) return;

    /* Free any previous mesh data for this body. */
    free(ctx->body_fvma[body_index]);

    ctx->body_fvma[body_index] = (uint8_t *)malloc(fvma_size);
    if (ctx->body_fvma[body_index]) {
        memcpy(ctx->body_fvma[body_index], fvma_data, fvma_size);
        ctx->body_fvma_size[body_index] = fvma_size;
        ctx->body_shape_type[body_index] = 5; /* custom mesh */
    }
}

/**
 * @brief Bridge: destroy a physics body when editor deletes an entity.
 */
static void bridge_on_delete_(void *user_data, uint32_t entity_id,
                               uint32_t body_index) {
    demo_ctx_t *ctx = (demo_ctx_t *)user_data;
    (void)entity_id;

    if (body_index < DEMO_MAX_BODIES) {
        phys_cmd_destroy_body_t destroy = {.body_index = body_index};
        phys_cmd_push(ctx->cmd_channel, PHYS_CMD_DESTROY_BODY,
                      &destroy, sizeof(destroy));
    }
}

/**
 * @brief Bridge: teleport a physics body when editor moves an entity.
 */
static void bridge_on_move_(void *user_data, uint32_t entity_id,
                             uint32_t body_index, const float pos[3]) {
    demo_ctx_t *ctx = (demo_ctx_t *)user_data;
    (void)entity_id;

    if (body_index < DEMO_MAX_BODIES) {
        phys_cmd_set_position_t setpos = {
            .body_index = body_index,
            .position = {pos[0], pos[1], pos[2]},
        };
        phys_cmd_push(ctx->cmd_channel, PHYS_CMD_SET_POSITION,
                      &setpos, sizeof(setpos));
    }
}

/**
 * @brief Bridge: query which entities are touching a given entity.
 *
 * Iterates all editor entities, performs bounding sphere pre-filter,
 * then full narrowphase collision test via phys_test_overlap().
 * Returns entity IDs (not body indices) in out_entity_ids.
 */
static uint32_t bridge_on_query_touching_(void *user_data,
                                           uint32_t entity_id,
                                           const uint32_t *candidates,
                                           uint32_t candidate_count,
                                           uint32_t *out_entity_ids,
                                           uint32_t max_results) {
    demo_ctx_t *ctx = (demo_ctx_t *)user_data;
    phys_world_t *world = &ctx->world;
    edit_entity_store_t *store = ctx->editor.dispatch.user_data
        ? ((edit_cmd_ctx_t *)ctx->editor.dispatch.user_data)->entities
        : NULL;
    if (!store) return 0;

    /* Look up the source entity and its body. */
    const edit_entity_t *src = edit_entity_store_get(store, entity_id);
    if (!src || !src->active) return 0;
    if (src->body_index >= DEMO_MAX_BODIES) return 0;
    if (!phys_body_pool_is_active(&world->body_pool, src->body_index))
        return 0;

    const phys_body_t *body_a = phys_body_pool_get_curr(
        &world->body_pool, src->body_index);
    const phys_collider_t *col_a = phys_world_get_collider(
        world, src->body_index);
    if (!body_a || !col_a) return 0;

    /* Build overlap context from world shape pools. */
    phys_overlap_ctx_t ov_ctx = {
        .spheres      = world->spheres,
        .boxes        = world->boxes,
        .capsules     = world->capsules,
        .meshes       = world->meshes,
        .convex_hulls = world->convex_hulls,
        .halfspaces   = world->halfspaces,
        .compounds    = world->compounds,
    };

    uint32_t found = 0;

    if (candidates && candidate_count > 0) {
        /* Iterate only candidate entities (group_mask optimization). */
        for (uint32_t c = 0; c < candidate_count && found < max_results;
             c++) {
            uint32_t eid = candidates[c];
            if (eid == entity_id) continue;
            const edit_entity_t *other = edit_entity_store_get(store, eid);
            if (!other || !other->active) continue;
            if (other->body_index >= DEMO_MAX_BODIES) continue;
            if (!phys_body_pool_is_active(&world->body_pool,
                                          other->body_index))
                continue;

            const phys_body_t *body_b = phys_body_pool_get_curr(
                &world->body_pool, other->body_index);
            const phys_collider_t *col_b = phys_world_get_collider(
                world, other->body_index);
            if (!body_b || !col_b) continue;

            if (phys_test_overlap(&ov_ctx,
                                  col_a, body_a->position,
                                  body_a->orientation,
                                  col_b, body_b->position,
                                  body_b->orientation)) {
                out_entity_ids[found++] = eid;
            }
        }
    } else {
        /* No candidate filter — iterate all entities. */
        for (uint32_t eid = 0;
             eid < store->capacity && found < max_results; eid++) {
            if (eid == entity_id) continue;
            const edit_entity_t *other = edit_entity_store_get(store, eid);
            if (!other || !other->active) continue;
            if (other->body_index >= DEMO_MAX_BODIES) continue;
            if (!phys_body_pool_is_active(&world->body_pool,
                                          other->body_index))
                continue;

            const phys_body_t *body_b = phys_body_pool_get_curr(
                &world->body_pool, other->body_index);
            const phys_collider_t *col_b = phys_world_get_collider(
                world, other->body_index);
            if (!body_b || !col_b) continue;

            if (phys_test_overlap(&ov_ctx,
                                  col_a, body_a->position,
                                  body_a->orientation,
                                  col_b, body_b->position,
                                  body_b->orientation)) {
                out_entity_ids[found++] = eid;
            }
        }
    }

    return found;
}

/* ── Bridge: create a physics joint ───────────────────────────────── */

/**
 * @brief Bridge callback: create a physics joint between two bodies.
 *
 * Initializes a phys_joint_t, sets anchors and axis, then adds it
 * to the physics world.  Updates body_constrained[] and joint_pairs[].
 */
static uint32_t bridge_on_joint_(void *user_data,
                                  uint32_t body_a, uint32_t body_b,
                                  int joint_type,
                                  const float local_anchor_a[3],
                                  const float local_anchor_b[3],
                                  const float axis[3]) {
    demo_ctx_t *ctx = (demo_ctx_t *)user_data;

    /* Defer joint creation to the physics tick via the command channel.
     * Bodies are also deferred (PHYS_CMD_SPAWN_BODY), so the joint must
     * be drained after the spawns in the same tick. */
    phys_cmd_add_joint_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.body_a     = body_a;
    cmd.body_b     = body_b;
    cmd.joint_type = joint_type;
    cmd.local_anchor_a = (phys_vec3_t){
        local_anchor_a[0], local_anchor_a[1], local_anchor_a[2]};
    cmd.local_anchor_b = (phys_vec3_t){
        local_anchor_b[0], local_anchor_b[1], local_anchor_b[2]};
    cmd.axis = (phys_vec3_t){axis[0], axis[1], axis[2]};

    if (!phys_cmd_push(ctx->cmd_channel, PHYS_CMD_ADD_JOINT,
                       &cmd, sizeof(cmd))) {
        fprintf(stderr, "[server] joint push failed (channel full?)\n");
        return UINT32_MAX;
    }

    /* Mark bodies as constrained for network priority. */
    if (body_a < DEMO_MAX_BODIES) ctx->body_constrained[body_a] = 1;
    if (body_b < DEMO_MAX_BODIES) ctx->body_constrained[body_b] = 1;

    /* Record joint pair for priority sender. */
    if (ctx->joint_pair_count + 1 < DEMO_MAX_BODIES) {
        uint32_t idx = ctx->joint_pair_count * 2;
        ctx->joint_pairs[idx]     = body_a;
        ctx->joint_pairs[idx + 1] = body_b;
        ctx->joint_pair_count++;
    }

    printf("[server] joint queued: type=%d bodies=(%u,%u)\n",
           joint_type, body_a, body_b);
    /* Return 0 as a success indicator (actual joint index assigned at drain). */
    return 0;
}

/* ── Physics simulation control callbacks ─────────────────────────── */

/** @brief Pause the physics tick runner. */
static void ctrl_on_pause_(void *user_data) {
    demo_ctx_t *ctx = (demo_ctx_t *)user_data;
    phys_tick_runner_pause(&ctx->tick_runner);
    printf("[server] physics paused\n");
}

/** @brief Resume the physics tick runner. */
static void ctrl_on_resume_(void *user_data) {
    demo_ctx_t *ctx = (demo_ctx_t *)user_data;
    phys_tick_runner_resume(&ctx->tick_runner);
    printf("[server] physics resumed\n");
}

/** @brief Step exactly one physics tick while paused. */
static void ctrl_on_step_(void *user_data) {
    demo_ctx_t *ctx = (demo_ctx_t *)user_data;
    phys_tick_runner_step_once(&ctx->tick_runner);
}

/** @brief Reset physics — zero all velocities. */
static void ctrl_on_reset_(void *user_data) {
    demo_ctx_t *ctx = (demo_ctx_t *)user_data;
    phys_body_pool_t *pool = &ctx->world.body_pool;
    for (uint32_t i = 0; i < pool->count; i++) {
        phys_body_t *b = &pool->bodies_next[i];
        b->linear_vel  = (phys_vec3_t){0, 0, 0};
        b->angular_vel = (phys_vec3_t){0, 0, 0};
    }
    printf("[server] physics reset (velocities zeroed)\n");
}

/** @brief Query whether physics is paused. */
static bool ctrl_is_paused_(void *user_data) {
    demo_ctx_t *ctx = (demo_ctx_t *)user_data;
    return phys_tick_runner_is_paused(&ctx->tick_runner);
}

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
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
        if (ctx->body_constrained[bi]) {
            spawn_msg.flags |= 0x04u; /* bit2 = constrained (interpolated) */
        }
        if (ctx->body_hidden[bi]) {
            spawn_msg.flags |= 0x08u; /* bit3 = hidden (trigger volume) */
        }
        spawn_msg.shape_type = ctx->body_shape_type[bi];
        spawn_msg.color_seed = ctx->body_color[bi];

        net_qvec3_mm_t qpos;
        net_quantize_vec3_mm(
            (vec3_t){body->position.x, body->position.y, body->position.z},
            &qpos);
        spawn_msg.pos_mm = (net_repl_vec3_mm_t){qpos.x_mm, qpos.y_mm, qpos.z_mm};

        spawn_msg.rot_x = body->orientation.x;
        spawn_msg.rot_y = body->orientation.y;
        spawn_msg.rot_z = body->orientation.z;
        spawn_msg.rot_w = body->orientation.w;

        /* Encode per-body half-extents as float16 (meters). */
        spawn_msg.half_x_f16 = net_float16_from_float(ctx->body_half[bi][0]);
        spawn_msg.half_y_f16 = net_float16_from_float(ctx->body_half[bi][1]);
        spawn_msg.half_z_f16 = net_float16_from_float(ctx->body_half[bi][2]);

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

/* ── Mesh data chunk sender ────────────────────────────────────── */

/** @brief Context for the per-chunk send callback. */
typedef struct {
    fr_topic_channel_t *channel;
    uint32_t            chunks_sent;
} mesh_send_ctx_t;

/**
 * @brief Callback for net_repl_mesh_data_send: pushes each chunk
 *        onto the reliable topic channel.
 */
static bool mesh_send_chunk_cb_(const uint8_t *wire, size_t wire_len,
                                 void *user_data) {
    mesh_send_ctx_t *mctx = (mesh_send_ctx_t *)user_data;
    if (fr_topic_channel_push(mctx->channel, wire, wire_len)) {
        mctx->chunks_sent++;
        return true;
    }
    fprintf(stderr, "[server] WARN: mesh chunk push failed\n");
    return false;
}

/**
 * @brief Send FVMA mesh data to a client for any mesh bodies that
 *        have been spawned but whose mesh data hasn't been sent yet.
 */
static void send_mesh_data_to_client(demo_ctx_t *ctx, uint16_t client_id) {
    fr_topic_channel_t *reliable = NULL;
    fr_topic_channel_t *unreliable = NULL;
    if (!fr_server_net_runtime_client_out_topics(ctx->net_rt, client_id,
                                                  &reliable, &unreliable)) {
        return;
    }
    if (!reliable) return;

    for (uint32_t bi = 0; bi < DEMO_MAX_BODIES; bi++) {
        size_t idx = (size_t)client_id * DEMO_MAX_BODIES + bi;

        /* Only send mesh data after body spawn has been sent. */
        if (!ctx->spawned_to_client[idx]) continue;
        /* Skip if already sent. */
        if (ctx->mesh_sent_to_client[idx]) continue;
        /* Skip non-mesh bodies. */
        if (!ctx->body_fvma[bi] || ctx->body_fvma_size[bi] == 0) {
            ctx->mesh_sent_to_client[idx] = 1u; /* nothing to send */
            continue;
        }

        mesh_send_ctx_t mctx = {.channel = reliable, .chunks_sent = 0};
        uint32_t sent = net_repl_mesh_data_send(
            (uint16_t)bi,
            ctx->body_fvma[bi],
            ctx->body_fvma_size[bi],
            mesh_send_chunk_cb_,
            &mctx);
        if (sent > 0) {
            ctx->mesh_sent_to_client[idx] = 1u;
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

/* ── Script tick helper ─────────────────────────────────────────── */

/**
 * @brief Per-tick script maintenance.
 *
 * Rebuilds the entity snapshot so scripts see fresh state,
 * drains completed async tasks (raycasts), and ticks idle
 * tracking for auto-unscheduling exited scripts.
 */
static void script_tick(demo_ctx_t *ctx) {
    /* Rebuild entity snapshot from the store. */
    uint32_t snap_count = script_snapshot_build(
        &ctx->editor.entities,
        ctx->script_snapshots,
        DEMO_SCRIPT_MAX_ENTITIES);

    ctx->script_entity_view.entities = ctx->script_snapshots;
    ctx->script_entity_view.count    = snap_count;
    ctx->script_entity_view.capacity = DEMO_SCRIPT_MAX_ENTITIES;

    /* Propagate updated view to all active VMs. */
    aegis_script_runtime_set_entity_view(
        &ctx->script_runtime, &ctx->script_entity_view);

    /* Drain completed async tasks (raycasts against physics world). */
    aegis_async_execute_drain(
        &ctx->script_async_buf, &ctx->world, 32);

    /* Tick idle tracking — auto-unschedule scripts that exited and
     * haven't received a new event within the grace window. */
    aegis_runtime_tick_idle(&ctx->script_runtime);
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

                /* Cache address for physics-thread priority sends. */
                net_udp_addr_t caddr;
                if (fr_server_net_runtime_client_addr(ctx->net_rt,
                                                       client_id, &caddr)) {
                    ctx->client_addrs[client_id] = caddr;
                    ctx->client_addr_active[client_id] = 1u;
                }

                printf("[server] client %u joined (total: %u)\n",
                       client_id, ctx->clients_connected);

                /* Tell entity pump to spawn this player to remote clients. */
                fr_server_entity_net_pump_set_player_should_spawn_remote(
                    ctx->entity_pump, client_id, true);

                /* Send all existing body spawns to the new client. */
                send_body_spawns_to_client(ctx, client_id);
                send_mesh_data_to_client(ctx, client_id);
            }
        }
    }

    /* Send any new body spawns to already-connected clients. */
    for (uint16_t ci = 0; ci < DEMO_MAX_CLIENTS; ++ci) {
        if (ctx->client_joined[ci]) {
            send_body_spawns_to_client(ctx, ci);
            send_mesh_data_to_client(ctx, ci);
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
        /* For now, we acknowledge but don't process move inputs. */
        (void)evt;
    }

    /* Script maintenance: refresh entity snapshots, drain async tasks. */
    script_tick(ctx);
}

/**
 * Stage 2: physics (no-op—tick runner is async fiber).
 */
static void on_physics(void *user) {
    (void)user;
}

/**
 * Post-tick callback: send velocity-proportional priority BODY_STATE
 * updates for constrained bodies via raw UDP.
 *
 * Runs on the physics thread at physics tick rate (~60Hz).
 * Delegates to fr_priority_body_sender_tick which rate-limits per body
 * based on speed.
 */
static void on_post_physics_tick(void *user, uint64_t tick) {
    demo_ctx_t *ctx = (demo_ctx_t *)user;
    if (!ctx->priority_sender) { return; }

    net_udp_socket_t *raw_sock = fr_server_net_runtime_socket(ctx->net_rt);
    if (!raw_sock) { return; }

    fr_priority_body_sender_tick(
        ctx->priority_sender,
        &ctx->world,
        ctx->body_constrained,
        tick,
        raw_sock,
        ctx->client_addrs,
        ctx->client_addr_active,
        DEMO_MAX_CLIENTS,
        ctx->joint_pairs,
        ctx->joint_pair_count);
}

/**
 * Stage 3: encode replication — snapshot encode + chunk split + raw UDP send.
 *
 * Encodes the full physics world into a compact binary snapshot,
 * splits it into MTU-safe chunks, and sends each chunk directly
 * to every connected client as a raw UDP datagram (bypassing RUDP).
 */
static void on_encode(void *user) {
    demo_ctx_t *ctx = (demo_ctx_t *)user;
    ctx->server_tick++;

    /* Encode physics world into snapshot wire format. */
    size_t snap_len = phys_snapshot_encode(
        &ctx->world, ctx->snap_buf, sizeof(ctx->snap_buf));
    if (snap_len == 0) {
        return;
    }

    /* Split into MTU-safe chunks. */
    net_chunk_header_t headers[DEMO_SNAPSHOT_MAX_CHUNKS];
    uint32_t chunk_count = 0;
    if (net_snapshot_chunk_split(ctx->snap_buf, (uint32_t)snap_len,
                                 DEMO_SNAPSHOT_CHUNK_SIZE,
                                 headers, DEMO_SNAPSHOT_MAX_CHUNKS,
                                 &chunk_count) != NET_CHUNK_OK) {
        return;
    }

    /* Get the raw UDP socket for direct sends. */
    net_udp_socket_t *raw_sock = fr_server_net_runtime_socket(ctx->net_rt);
    if (!raw_sock) { return; }

    /* Send each chunk to every connected client as a raw UDP datagram. */
    for (uint32_t c = 0; c < chunk_count; c++) {
        /* Build wire message: [schema:2][chunk_idx:2][chunk_total:2]
         *                     [offset:4][length:4][data:N]           */
        uint8_t msg[DEMO_CHUNK_HDR_WIRE + DEMO_SNAPSHOT_CHUNK_SIZE];
        const net_chunk_header_t *h = &headers[c];

        /* Schema ID (LE). */
        msg[0] = (uint8_t)(NET_REPL_SCHEMA_SNAPSHOT_CHUNK & 0xFFu);
        msg[1] = (uint8_t)((NET_REPL_SCHEMA_SNAPSHOT_CHUNK >> 8u) & 0xFFu);
        /* Chunk index (LE). */
        msg[2] = (uint8_t)(h->chunk_index & 0xFFu);
        msg[3] = (uint8_t)((h->chunk_index >> 8u) & 0xFFu);
        /* Chunk total (LE). */
        msg[4] = (uint8_t)(h->chunk_total & 0xFFu);
        msg[5] = (uint8_t)((h->chunk_total >> 8u) & 0xFFu);
        /* Offset (LE). */
        memcpy(msg + 6, &h->offset, 4);
        /* Length (LE). */
        memcpy(msg + 10, &h->length, 4);
        /* Chunk data. */
        memcpy(msg + DEMO_CHUNK_HDR_WIRE, ctx->snap_buf + h->offset, h->length);
        size_t msg_len = DEMO_CHUNK_HDR_WIRE + h->length;

        for (uint16_t cid = 0; cid < DEMO_MAX_CLIENTS; cid++) {
            if (!ctx->client_joined[cid]) { continue; }
            net_udp_addr_t addr;
            if (!fr_server_net_runtime_client_addr(ctx->net_rt, cid, &addr)) {
                continue;
            }
            net_udp_socket_sendto(raw_sock, &addr, msg, msg_len);
        }
    }
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
        fprintf(stderr,
            "Usage: %s <port> [duration_s] [--net-workers N] [--phys-workers N]"
            " [--edit-port PORT]"
#ifdef FR_NET_EMULATION
            " [--emu-delay MS] [--emu-jitter MS] [--emu-loss PCT]"
            " [--emu-reorder PCT] [--emu-duplicate PCT]"
            " [--emu-dist uniform|normal|lognormal]"
#endif
            "\n", argv[0]);
        return 1;
    }
    uint16_t port = (uint16_t)atoi(argv[1]);
    double duration = (argc >= 3 && argv[2][0] != '-') ? atof(argv[2]) : 0.0;

    /* Default worker counts. */
    uint32_t net_workers  = 1u;
    uint32_t phys_workers = 6u;
    uint16_t edit_port    = 9100u; /* Editor protocol port. */

#ifdef FR_NET_EMULATION
    /* Network emulation parameters (all zero = disabled). */
    float emu_delay = 0.0f, emu_jitter = 0.0f, emu_loss = 0.0f;
    float emu_reorder = 0.0f, emu_duplicate = 0.0f;
    net_emu_distribution_t emu_dist = NET_EMU_DIST_UNIFORM;
#endif

    /* Parse optional flags. */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--net-workers") == 0 && i + 1 < argc) {
            net_workers = (uint32_t)atoi(argv[++i]);
            if (net_workers < 1u) net_workers = 1u;
        } else if (strcmp(argv[i], "--phys-workers") == 0 && i + 1 < argc) {
            phys_workers = (uint32_t)atoi(argv[++i]);
            if (phys_workers < 1u) phys_workers = 1u;
        } else if (strcmp(argv[i], "--edit-port") == 0 && i + 1 < argc) {
            edit_port = (uint16_t)atoi(argv[++i]);
        }
#ifdef FR_NET_EMULATION
        else if (strcmp(argv[i], "--emu-delay") == 0 && i + 1 < argc) {
            emu_delay = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--emu-jitter") == 0 && i + 1 < argc) {
            emu_jitter = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--emu-loss") == 0 && i + 1 < argc) {
            emu_loss = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--emu-reorder") == 0 && i + 1 < argc) {
            emu_reorder = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--emu-duplicate") == 0 && i + 1 < argc) {
            emu_duplicate = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--emu-dist") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "normal") == 0)        emu_dist = NET_EMU_DIST_NORMAL;
            else if (strcmp(argv[i], "lognormal") == 0) emu_dist = NET_EMU_DIST_LOG_NORMAL;
            else                                        emu_dist = NET_EMU_DIST_UNIFORM;
        }
#endif
    }

#ifdef FR_NET_EMULATION
    /* Configure engine settings before any threads start. */
    fr_engine_settings_init();
    {
        fr_engine_settings_t *s = fr_engine_settings_mut();
        int has_emu = (emu_delay > 0.0f || emu_jitter > 0.0f ||
                       emu_loss > 0.0f || emu_reorder > 0.0f ||
                       emu_duplicate > 0.0f);
        s->net_emu_enabled = has_emu ? 1 : 0;
        s->net_emu.delay_ms      = emu_delay;
        s->net_emu.jitter_ms     = emu_jitter;
        s->net_emu.loss_pct      = emu_loss;
        s->net_emu.reorder_pct   = emu_reorder;
        s->net_emu.duplicate_pct = emu_duplicate;
        s->net_emu.distribution  = emu_dist;
        if (has_emu) {
            printf("[server] net emulation: delay=%.1fms jitter=%.1fms "
                   "loss=%.1f%% reorder=%.1f%% dup=%.1f%% dist=%d\n",
                   emu_delay, emu_jitter, emu_loss,
                   emu_reorder, emu_duplicate, (int)emu_dist);
        }
    }
    fr_engine_settings_freeze();
#endif

    signal(SIGINT, handle_sigint);

    demo_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    /* ── 1. Job systems ───────────────────────────────────────────── */
    /* Networking job system: client fibers, demux pump, entity pump. */
    if (job_system_create(&ctx.job_sys, net_workers, 256u, DEMO_FIBER_STACK,
                          256u, 0) != JOB_CREATE_OK) {
        fprintf(stderr, "error: job_system_create (net) failed\n");
        return 1;
    }
    if (job_system_start(&ctx.job_sys) != 0) {
        fprintf(stderr, "error: job_system_start (net) failed\n");
        return 1;
    }
    /* Physics job system: parallel stages (broadphase, narrow, solve, etc). */
    if (job_system_create(&ctx.phys_job_sys, phys_workers, 4096u, DEMO_FIBER_STACK,
                          4096u, 0) != JOB_CREATE_OK) {
        fprintf(stderr, "error: job_system_create (phys) failed\n");
        return 1;
    }
    if (job_system_start(&ctx.phys_job_sys) != 0) {
        fprintf(stderr, "error: job_system_start (phys) failed\n");
        return 1;
    }
    printf("[server] job systems started (net=%u workers, phys=%u workers)\n",
           net_workers, phys_workers);

    /* ── 2. Physics world ──────────────────────────────────────── */
    phys_world_config_t wcfg = phys_world_config_default();
    wcfg.max_bodies = DEMO_MAX_BODIES;
    wcfg.max_colliders = DEMO_MAX_BODIES;
    if (phys_world_init(&ctx.world, &wcfg) != 0) {
        fprintf(stderr, "error: phys_world_init failed\n");
        return 1;
    }
    printf("[server] physics world created (max %u bodies)\n", DEMO_MAX_BODIES);
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

    /* Now that net_rt and socket are ready, enable post-tick
     * priority updates for constrained bodies on the physics thread. */
    fr_priority_body_sender_config_t pbs_cfg = {
        .max_bodies     = DEMO_MAX_BODIES,
        .speed_full_rate = 10.0f,  /* 10 m/s → every physics tick */
        .speed_min       = 0.3f,   /* < 0.3 m/s → skip entirely */
        .max_interval    = 30,     /* slowest movers: every 30 ticks (~0.5s) */
    };
    ctx.priority_sender = fr_priority_body_sender_create(&pbs_cfg);

    ctx.tick_runner.post_tick_cb      = on_post_physics_tick;
    ctx.tick_runner.post_tick_cb_user = &ctx;

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

    /* ── 8. (body state broadcaster removed — using snapshot encode) ── */

    /* ── 9. Spawned-to-client tracking ─────────────────────────── */
    ctx.spawned_to_client = (uint8_t *)calloc(
        (size_t)DEMO_MAX_CLIENTS * (size_t)DEMO_MAX_BODIES, 1u);
    ctx.mesh_sent_to_client = (uint8_t *)calloc(
        (size_t)DEMO_MAX_CLIENTS * (size_t)DEMO_MAX_BODIES, 1u);
    if (!ctx.spawned_to_client || !ctx.mesh_sent_to_client) {
        fprintf(stderr, "error: spawn/mesh tracking alloc failed\n");
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

    /* ── 10b. Editor context ──────────────────────────────────── */
    ctx.edit_port = edit_port;
    {
        editor_ctx_config_t ecfg = {.edit_port = edit_port};
        if (!editor_ctx_init(&ctx.editor, &ecfg)) {
            fprintf(stderr, "error: editor_ctx_init failed\n");
            return 1;
        }
        /* Wire physics bridge. */
        ctx.editor_bridge = (edit_physics_bridge_t){
            .on_spawn  = bridge_on_spawn_,
            .on_delete = bridge_on_delete_,
            .on_move   = bridge_on_move_,
            .on_query_touching = bridge_on_query_touching_,
            .on_mesh_data = bridge_on_mesh_data_,
            .on_joint  = bridge_on_joint_,
            .user_data = &ctx,
        };
        editor_ctx_set_bridge(&ctx.editor, &ctx.editor_bridge);

        /* Wire physics simulation control. */
        ctx.physics_ctrl = (edit_physics_ctrl_t){
            .on_pause  = ctrl_on_pause_,
            .on_resume = ctrl_on_resume_,
            .on_step   = ctrl_on_step_,
            .on_reset  = ctrl_on_reset_,
            .is_paused = ctrl_is_paused_,
            .user_data = &ctx,
        };
        editor_ctx_set_physics(&ctx.editor, &ctx.physics_ctrl);

        /* Start physics paused in editor mode. */
        phys_tick_runner_pause(&ctx.tick_runner);

        printf("[server] editor listening on port %u (physics paused)\n",
               ctx.editor.io_thread.port);
    }

    /* ── 10c. Script runtime ─────────────────────────────────── */
    {
        aegis_runtime_config_t scfg = {0};
        scfg.max_instances      = 64;
        scfg.max_subscriptions  = 256;
        scfg.event_queue_cap    = 64;
        scfg.vm_config          = aegis_config_default();
        scfg.vm_config.arena_size  = 8192;
        scfg.vm_config.static_max = 512;
        scfg.vm_config.stack_max  = 512;
        scfg.signal_rate_limit_us  = 250;
        scfg.idle_grace_ticks      = 6;

        if (!aegis_script_runtime_init(&ctx.script_runtime, &scfg)) {
            fprintf(stderr, "error: aegis_script_runtime_init failed\n");
            return 1;
        }
        aegis_script_runtime_set_job_sys(&ctx.script_runtime, &ctx.job_sys);

        /* Async task buffer for VIS_TEST / NAV_QUERY. */
        if (!aegis_async_buffer_init(&ctx.script_async_buf, DEMO_ASYNC_TASK_CAP)) {
            fprintf(stderr, "error: aegis_async_buffer_init failed\n");
            return 1;
        }
        aegis_script_runtime_set_async_buffer(
            &ctx.script_runtime, &ctx.script_async_buf);

        /* Wire runtime into editor command context. */
        ctx.editor.cmd_ctx.script_runtime = &ctx.script_runtime;

        printf("[server] script runtime ready (max %u instances)\n",
               scfg.max_instances);
    }

    /* ── 11. Main loop ─────────────────────────────────────────── */
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

        /* Drain editor commands (Stage 1 — before physics). */
        editor_tick_drain(&ctx.editor);

        /* Fixed-rate server ticks via tick loop. */
        fr_server_tick_loop_step(&ctx.tick_loop, elapsed_us);

        /* Periodic stats (once per second). */
        uint64_t tick_id = fr_server_tick_loop_tick_id(&ctx.tick_loop);
        if (tick_id > 0 && tick_id % DEMO_TICK_HZ == 0 &&
            tick_id != ctx.last_stats_tick) {
            ctx.last_stats_tick = tick_id;
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

    /* Destroy script runtime before job system (fibers must finish). */
    aegis_script_runtime_destroy(&ctx.script_runtime);
    aegis_async_buffer_destroy(&ctx.script_async_buf);
    printf("[server] script runtime stopped\n");

    /* Shut down editor first (stops I/O thread). */
    editor_ctx_shutdown(&ctx.editor);
    printf("[server] editor stopped\n");

    /* Stop physics tick runner thread (joins the dedicated thread). */
    phys_tick_runner_stop(&ctx.tick_runner);
    printf("[server] tick runner stopped\n");

    /* Tear down in reverse init order. */
    fr_priority_body_sender_destroy(ctx.priority_sender);
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
    free(ctx.mesh_sent_to_client);
    for (uint32_t i = 0; i < DEMO_MAX_BODIES; i++) {
        free(ctx.body_fvma[i]);
    }

    uint64_t final_tick = fr_server_tick_loop_tick_id(&ctx.tick_loop);
    printf("[server] done. %lu ticks, %u bodies spawned.\n",
           (unsigned long)final_tick, ctx.total_spawned);
    return 0;
}

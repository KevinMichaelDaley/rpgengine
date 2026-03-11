/**
 * @file phys_cmd_drain.c
 * @brief Drain a topic channel of physics commands and apply them.
 *
 * Three drain modes:
 *
 * 1. phys_cmd_drain() — legacy, drains everything to bodies_curr.
 *    Only safe when no concurrent readers exist (e.g., paused state).
 *
 * 2. phys_cmd_drain_spawns() — thread-safe split drain.  Structural
 *    commands (spawn/destroy/joint) execute immediately on bodies_curr
 *    (safe because new slots don't contend with existing reads).
 *    Mutation commands (set_position, impulse, state, material) are
 *    staged into a deferred list.
 *
 * 3. phys_cmd_apply_mutations() — applies deferred mutations to a
 *    target body buffer (typically bodies_next) before a buffer swap.
 *    This ensures mutations appear atomically to concurrent readers
 *    of bodies_curr.
 */

#include "ferrum/physics/phys_cmd.h"
#include "ferrum/physics/world.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/joint.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/net/topic_channel.h"

#include <string.h>

/* ── Helpers ───────────────────────────────────────────────────── */

/** Apply a single SPAWN_BODY command. */
static void apply_spawn_(phys_world_t *world,
                          const phys_cmd_spawn_body_t *cmd,
                          phys_cmd_spawn_callback_t cb,
                          void *cb_user) {
    uint32_t idx = phys_world_create_body(world);
    if (idx == UINT32_MAX) {
        if (cb) { cb(UINT32_MAX, cmd->user_tag, cb_user); }
        return;
    }

    phys_body_t *b = phys_world_get_body(world, idx);
    b->position    = cmd->position;
    b->orientation = cmd->orientation;
    b->linear_vel  = cmd->linear_vel;
    b->flags       = cmd->flags;

    if (cmd->mass > 0.0f) {
        phys_body_set_mass(b, cmd->mass);

        switch (cmd->shape) {
        case PHYS_CMD_SHAPE_BOX:
            phys_body_set_box_inertia(b, cmd->mass, cmd->shape_data.box_half);
            break;
        case PHYS_CMD_SHAPE_SPHERE:
            phys_body_set_sphere_inertia(b, cmd->mass, cmd->shape_data.sphere_r);
            break;
        case PHYS_CMD_SHAPE_CAPSULE:
            phys_body_set_capsule_inertia(b, cmd->mass,
                cmd->shape_data.capsule.radius,
                cmd->shape_data.capsule.half_height);
            break;
        case PHYS_CMD_SHAPE_HALFSPACE:
            /* Halfspaces are always static — no inertia needed. */
            break;
        }
    }

    /* Apply material properties if explicitly provided. */
    if (cmd->has_material) {
        b->friction    = cmd->friction;
        b->restitution = cmd->restitution;
    }

    /* Apply tier override if specified. */
    if (cmd->tier > 0) {
        b->tier = cmd->tier;
    }

    /* Attach collider. */
    phys_vec3_t zero_off = {0.0f, 0.0f, 0.0f};
    phys_quat_t identity = {0.0f, 0.0f, 0.0f, 1.0f};

    switch (cmd->shape) {
    case PHYS_CMD_SHAPE_BOX:
        phys_world_set_box_collider(world, idx, cmd->shape_data.box_half,
                                    zero_off, identity);
        break;
    case PHYS_CMD_SHAPE_SPHERE:
        phys_world_set_sphere_collider(world, idx, cmd->shape_data.sphere_r,
                                       zero_off);
        break;
    case PHYS_CMD_SHAPE_CAPSULE:
        phys_world_set_capsule_collider(world, idx,
                                        cmd->shape_data.capsule.radius,
                                        cmd->shape_data.capsule.half_height,
                                        zero_off, identity);
        break;
    case PHYS_CMD_SHAPE_HALFSPACE:
        phys_world_set_halfspace_collider(world, idx,
                                          cmd->shape_data.halfspace.normal,
                                          cmd->shape_data.halfspace.distance);
        break;
    }

    if (cb) { cb(idx, cmd->user_tag, cb_user); }
}

/** Apply a single SET_POSITION command.
 *  Only writes bodies_curr — safe to call after the tick's buffer swap. */
static void apply_set_position_(phys_world_t *world,
                                 const phys_cmd_set_position_t *cmd) {
    phys_body_t *b = phys_world_get_body(world, cmd->body_index);
    if (!b) { return; }
    b->position = cmd->position;
}

/** Apply a single APPLY_IMPULSE command.
 *  Only writes bodies_curr — safe to call after the tick's buffer swap. */
static void apply_impulse_(phys_world_t *world,
                            const phys_cmd_apply_impulse_t *cmd) {
    phys_body_t *b = phys_world_get_body(world, cmd->body_index);
    if (!b || b->inv_mass <= 0.0f) { return; }

    b->linear_vel.x += cmd->impulse.x * b->inv_mass;
    b->linear_vel.y += cmd->impulse.y * b->inv_mass;
    b->linear_vel.z += cmd->impulse.z * b->inv_mass;
}

/** Apply a single SET_STATE command (authoritative state correction).
 *
 *  Only writes fields whose bits are set in cmd->flags.  If flags==0 or
 *  flags==PHYS_SET_ALL the behaviour is the same as the legacy path
 *  (overwrite everything).  We clear the sleep flag so resting bodies
 *  respond to the correction. */
static void apply_set_state_(phys_world_t *world,
                              const phys_cmd_set_state_t *cmd) {
    phys_body_t *b = phys_world_get_body(world, cmd->body_index);
    if (!b) { return; }

    /* Treat flags==0 as "all" for backward compatibility with callers
     * that memset the struct and never set the flags field. */
    uint32_t f = cmd->flags ? cmd->flags : PHYS_SET_ALL;

    if (f & PHYS_SET_POS)     b->position    = cmd->position;
    if (f & PHYS_SET_ORI)     b->orientation = cmd->orientation;
    if (f & PHYS_SET_LIN_VEL) b->linear_vel  = cmd->linear_vel;
    if (f & PHYS_SET_ANG_VEL) b->angular_vel = cmd->angular_vel;

    /* Sleep heuristic based on the body's resulting velocity. */
    const float lx = b->linear_vel.x,  ly = b->linear_vel.y,  lz = b->linear_vel.z;
    const float ax = b->angular_vel.x, ay = b->angular_vel.y, az = b->angular_vel.z;
    const float speed_sq = lx*lx + ly*ly + lz*lz + ax*ax + ay*ay + az*az;
    if (speed_sq < 1e-4f) {
        b->flags |= (uint32_t)PHYS_BODY_FLAG_SLEEPING;
        b->sleep_counter = 255u;
    } else {
        b->flags &= ~(uint32_t)PHYS_BODY_FLAG_SLEEPING;
        b->sleep_counter = 0;
    }
}

/* ── Public API (1 non-static function) ────────────────────────── */

void phys_cmd_drain(phys_world_t *world,
                    fr_topic_channel_t *cmd_channel,
                    phys_cmd_spawn_callback_t spawn_cb,
                    void *spawn_cb_user) {
    if (!world || !cmd_channel) { return; }

    /* Temporary buffer — max command size is well under 256 bytes. */
    uint8_t buf[256];

    for (;;) {
        size_t len = sizeof(buf);
        if (!fr_topic_channel_pop(cmd_channel, buf, &len)) {
            break;
        }
        if (len < 1u) { continue; }

        phys_cmd_type_t type = (phys_cmd_type_t)buf[0];
        const uint8_t *payload = buf + 1u;
        size_t payload_len = len - 1u;

        switch (type) {
        case PHYS_CMD_SPAWN_BODY:
            if (payload_len >= sizeof(phys_cmd_spawn_body_t)) {
                phys_cmd_spawn_body_t cmd;
                memcpy(&cmd, payload, sizeof(cmd));
                apply_spawn_(world, &cmd, spawn_cb, spawn_cb_user);
            }
            break;

        case PHYS_CMD_SET_POSITION:
            if (payload_len >= sizeof(phys_cmd_set_position_t)) {
                phys_cmd_set_position_t cmd;
                memcpy(&cmd, payload, sizeof(cmd));
                apply_set_position_(world, &cmd);
            }
            break;

        case PHYS_CMD_APPLY_IMPULSE:
            if (payload_len >= sizeof(phys_cmd_apply_impulse_t)) {
                phys_cmd_apply_impulse_t cmd;
                memcpy(&cmd, payload, sizeof(cmd));
                apply_impulse_(world, &cmd);
            }
            break;

        case PHYS_CMD_DESTROY_BODY:
            if (payload_len >= sizeof(phys_cmd_destroy_body_t)) {
                phys_cmd_destroy_body_t cmd;
                memcpy(&cmd, payload, sizeof(cmd));
                phys_world_destroy_body(world, cmd.body_index);
            }
            break;

        case PHYS_CMD_SET_STATE:
            if (payload_len >= sizeof(phys_cmd_set_state_t)) {
                phys_cmd_set_state_t cmd;
                memcpy(&cmd, payload, sizeof(cmd));
                apply_set_state_(world, &cmd);
            }
            break;

        case PHYS_CMD_ADD_JOINT:
            if (payload_len >= sizeof(phys_cmd_add_joint_t)) {
                phys_cmd_add_joint_t cmd;
                memcpy(&cmd, payload, sizeof(cmd));
                phys_joint_t joint;
                phys_joint_init(&joint);
                joint.type   = (phys_joint_type_t)cmd.joint_type;
                joint.body_a = cmd.body_a;
                joint.body_b = cmd.body_b;
                joint.local_anchor_a = cmd.local_anchor_a;
                joint.local_anchor_b = cmd.local_anchor_b;
                joint.local_axis_a   = cmd.axis;
                phys_world_add_joint(world, &joint);
            }
            break;

        case PHYS_CMD_SET_MATERIAL:
            if (payload_len >= sizeof(phys_cmd_set_material_t)) {
                phys_cmd_set_material_t cmd;
                memcpy(&cmd, payload, sizeof(cmd));
                phys_body_t *mb = phys_world_get_body(world, cmd.body_index);
                if (mb) {
                    mb->friction    = cmd.friction;
                    mb->restitution = cmd.restitution;
                }
            }
            break;

        default:
            break; /* Unknown command — skip. */
        }
    }
}

/* ── Split drain: spawns immediate, mutations deferred ─────────── */

void phys_cmd_drain_spawns(phys_world_t *world,
                           fr_topic_channel_t *cmd_channel,
                           phys_cmd_spawn_callback_t spawn_cb,
                           void *spawn_cb_user,
                           phys_cmd_mutation_list_t *deferred) {
    if (!world || !cmd_channel) { return; }
    if (deferred) { deferred->used = 0; }

    uint8_t buf[256];

    for (;;) {
        size_t len = sizeof(buf);
        if (!fr_topic_channel_pop(cmd_channel, buf, &len)) { break; }
        if (len < 1u) { continue; }

        phys_cmd_type_t type = (phys_cmd_type_t)buf[0];
        const uint8_t *payload = buf + 1u;
        size_t payload_len = len - 1u;

        switch (type) {
        /* ── Structural commands: apply immediately to bodies_curr ── */
        case PHYS_CMD_SPAWN_BODY:
            if (payload_len >= sizeof(phys_cmd_spawn_body_t)) {
                phys_cmd_spawn_body_t cmd;
                memcpy(&cmd, payload, sizeof(cmd));
                apply_spawn_(world, &cmd, spawn_cb, spawn_cb_user);
            }
            break;

        case PHYS_CMD_DESTROY_BODY:
            if (payload_len >= sizeof(phys_cmd_destroy_body_t)) {
                phys_cmd_destroy_body_t cmd;
                memcpy(&cmd, payload, sizeof(cmd));
                phys_world_destroy_body(world, cmd.body_index);
            }
            break;

        case PHYS_CMD_ADD_JOINT:
            if (payload_len >= sizeof(phys_cmd_add_joint_t)) {
                phys_cmd_add_joint_t cmd;
                memcpy(&cmd, payload, sizeof(cmd));
                phys_joint_t joint;
                phys_joint_init(&joint);
                joint.type   = (phys_joint_type_t)cmd.joint_type;
                joint.body_a = cmd.body_a;
                joint.body_b = cmd.body_b;
                joint.local_anchor_a = cmd.local_anchor_a;
                joint.local_anchor_b = cmd.local_anchor_b;
                joint.local_axis_a   = cmd.axis;
                phys_world_add_joint(world, &joint);
            }
            break;

        /* ── Mutation commands: defer for bodies_next application ── */
        default:
            if (deferred && deferred->used + len <= deferred->cap) {
                memcpy(deferred->data + deferred->used, buf, len);
                deferred->used += len;
            }
            break;
        }
    }
}

/* ── Apply deferred mutations to a target body buffer ──────────── */

void phys_cmd_apply_mutations(const phys_cmd_mutation_list_t *mutations,
                              phys_body_t *target_bodies,
                              uint32_t body_cap) {
    if (!mutations || !target_bodies || mutations->used == 0) { return; }

    size_t offset = 0;
    while (offset < mutations->used) {
        phys_cmd_type_t type = (phys_cmd_type_t)mutations->data[offset];
        const uint8_t *payload = mutations->data + offset + 1;

        switch (type) {
        case PHYS_CMD_SET_POSITION: {
            size_t need = 1 + sizeof(phys_cmd_set_position_t);
            if (offset + need > mutations->used) goto done;
            phys_cmd_set_position_t cmd;
            memcpy(&cmd, payload, sizeof(cmd));
            if (cmd.body_index < body_cap) {
                target_bodies[cmd.body_index].position = cmd.position;
            }
            offset += need;
            break;
        }
        case PHYS_CMD_APPLY_IMPULSE: {
            size_t need = 1 + sizeof(phys_cmd_apply_impulse_t);
            if (offset + need > mutations->used) goto done;
            phys_cmd_apply_impulse_t cmd;
            memcpy(&cmd, payload, sizeof(cmd));
            if (cmd.body_index < body_cap) {
                phys_body_t *b = &target_bodies[cmd.body_index];
                if (b->inv_mass > 0.0f) {
                    b->linear_vel.x += cmd.impulse.x * b->inv_mass;
                    b->linear_vel.y += cmd.impulse.y * b->inv_mass;
                    b->linear_vel.z += cmd.impulse.z * b->inv_mass;
                }
            }
            offset += need;
            break;
        }
        case PHYS_CMD_SET_STATE: {
            size_t need = 1 + sizeof(phys_cmd_set_state_t);
            if (offset + need > mutations->used) goto done;
            phys_cmd_set_state_t cmd;
            memcpy(&cmd, payload, sizeof(cmd));
            if (cmd.body_index < body_cap) {
                phys_body_t *b = &target_bodies[cmd.body_index];
                uint32_t f = cmd.flags ? cmd.flags : PHYS_SET_ALL;
                if (f & PHYS_SET_POS)     b->position    = cmd.position;
                if (f & PHYS_SET_ORI)     b->orientation = cmd.orientation;
                if (f & PHYS_SET_LIN_VEL) b->linear_vel  = cmd.linear_vel;
                if (f & PHYS_SET_ANG_VEL) b->angular_vel = cmd.angular_vel;
                /* Sleep heuristic based on resulting velocity. */
                float lx = b->linear_vel.x, ly = b->linear_vel.y,
                      lz = b->linear_vel.z;
                float ax = b->angular_vel.x, ay = b->angular_vel.y,
                      az = b->angular_vel.z;
                float speed_sq = lx*lx + ly*ly + lz*lz + ax*ax + ay*ay + az*az;
                if (speed_sq < 1e-4f) {
                    b->flags |= (uint32_t)PHYS_BODY_FLAG_SLEEPING;
                    b->sleep_counter = 255u;
                } else {
                    b->flags &= ~(uint32_t)PHYS_BODY_FLAG_SLEEPING;
                    b->sleep_counter = 0;
                }
            }
            offset += need;
            break;
        }
        case PHYS_CMD_SET_MATERIAL: {
            size_t need = 1 + sizeof(phys_cmd_set_material_t);
            if (offset + need > mutations->used) goto done;
            phys_cmd_set_material_t cmd;
            memcpy(&cmd, payload, sizeof(cmd));
            if (cmd.body_index < body_cap) {
                target_bodies[cmd.body_index].friction    = cmd.friction;
                target_bodies[cmd.body_index].restitution = cmd.restitution;
            }
            offset += need;
            break;
        }
        default:
            /* Unknown type in mutation buffer — should not happen.
             * Bail to avoid infinite loop. */
            goto done;
        }
    }
done:
    return;
}

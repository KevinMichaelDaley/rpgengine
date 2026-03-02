/**
 * @file phys_cmd_drain.c
 * @brief Drain a topic channel of physics commands and apply them.
 *
 * Called by the tick fiber at the start of each physics tick, before
 * any simulation stages run.  This is the single point where external
 * mutations flow into the physics world.
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
        }
    }

    /* Copy to next buffer so the tick sees consistent state. */
    phys_body_t *b_next = phys_body_pool_get_next(&world->body_pool, idx);
    if (b_next) { *b_next = *b; }

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
    }

    if (cb) { cb(idx, cmd->user_tag, cb_user); }
}

/** Apply a single SET_POSITION command. */
static void apply_set_position_(phys_world_t *world,
                                 const phys_cmd_set_position_t *cmd) {
    phys_body_t *b = phys_world_get_body(world, cmd->body_index);
    if (!b) { return; }
    b->position = cmd->position;

    phys_body_t *b_next = phys_body_pool_get_next(&world->body_pool,
                                                    cmd->body_index);
    if (b_next) { b_next->position = cmd->position; }
}

/** Apply a single APPLY_IMPULSE command. */
static void apply_impulse_(phys_world_t *world,
                            const phys_cmd_apply_impulse_t *cmd) {
    phys_body_t *b = phys_world_get_body(world, cmd->body_index);
    if (!b || b->inv_mass <= 0.0f) { return; }

    b->linear_vel.x += cmd->impulse.x * b->inv_mass;
    b->linear_vel.y += cmd->impulse.y * b->inv_mass;
    b->linear_vel.z += cmd->impulse.z * b->inv_mass;

    phys_body_t *b_next = phys_body_pool_get_next(&world->body_pool,
                                                    cmd->body_index);
    if (b_next) { *b_next = *b; }
}

/** Apply a single SET_STATE command (full authoritative correction).
 *
 *  Hard-snaps position, orientation, and velocity to the server's
 *  authoritative values.  The client simulation is purely cosmetic
 *  between corrections — there is no rollback, so blending creates
 *  oscillation.  We clear the sleep flag so resting bodies respond. */
static void apply_set_state_(phys_world_t *world,
                              const phys_cmd_set_state_t *cmd) {
    phys_body_t *b = phys_world_get_body(world, cmd->body_index);
    if (!b) { return; }

    b->position    = cmd->position;
    b->orientation = cmd->orientation;
    b->linear_vel  = cmd->linear_vel;
    b->angular_vel = cmd->angular_vel;

    /* If the server sent near-zero velocity the body is at rest — mark
     * it sleeping so prediction mode doesn't keep integrating gravity.
     * The threshold must exceed quantization noise: velocities arrive as
     * int16 mm/s and mrad/s, so the minimum nonzero per-component
     * magnitude is 0.001.  With 6 components that gives speed_sq up to
     * ~6e-6 for a "resting" body.  Use 1e-4 (~10mm/s aggregate) to
     * absorb solver jitter and quantization rounding. */
    const float lx = cmd->linear_vel.x, ly = cmd->linear_vel.y, lz = cmd->linear_vel.z;
    const float ax = cmd->angular_vel.x, ay = cmd->angular_vel.y, az = cmd->angular_vel.z;
    const float speed_sq = lx*lx + ly*ly + lz*lz + ax*ax + ay*ay + az*az;
    if (speed_sq < 1e-4f) {
        b->flags |= (uint32_t)PHYS_BODY_FLAG_SLEEPING;
        b->sleep_counter = 255u;
    } else {
        b->flags &= ~(uint32_t)PHYS_BODY_FLAG_SLEEPING;
        b->sleep_counter = 0;
    }

    phys_body_t *b_next = phys_body_pool_get_next(&world->body_pool,
                                                     cmd->body_index);
    if (b_next) { *b_next = *b; }
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

        default:
            break; /* Unknown command — skip. */
        }
    }
}

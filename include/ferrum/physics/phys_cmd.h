/**
 * @file phys_cmd.h
 * @brief Physics command types for deferred world mutations.
 *
 * Commands are serialized into a fr_topic_channel_t by the main thread
 * and drained by the physics tick fiber before stepping the simulation.
 * This decouples the main loop from physics state, eliminating data
 * races and allowing the main thread to run without blocking.
 *
 * Wire format per command:
 *   [1 byte type] [payload bytes...]
 *
 * Thread safety: commands are pushed from one thread (main) and popped
 * from one fiber (physics tick).  The topic channel's spinlock handles
 * the synchronization.
 */

#ifndef FERRUM_PHYSICS_PHYS_CMD_H
#define FERRUM_PHYSICS_PHYS_CMD_H

#include "ferrum/physics/phys_vec3.h"
#include "ferrum/physics/phys_quat.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Command type discriminator (first byte of the serialized command). */
typedef enum phys_cmd_type {
    /** Create a rigid body with position, velocity, mass, shape, and flags.
     *  Payload: phys_cmd_spawn_body_t. */
    PHYS_CMD_SPAWN_BODY = 1,

    /** Set a kinematic body's position directly.
     *  Payload: phys_cmd_set_position_t. */
    PHYS_CMD_SET_POSITION = 2,

    /** Apply an impulse to a body's linear velocity.
     *  Payload: phys_cmd_apply_impulse_t. */
    PHYS_CMD_APPLY_IMPULSE = 3,

    /** Destroy a body by index.
     *  Payload: phys_cmd_destroy_body_t. */
    PHYS_CMD_DESTROY_BODY = 4,

    /** Set a body's full state (position, orientation, velocity).
     *  Used for authoritative server corrections on the client.
     *  Payload: phys_cmd_set_state_t. */
    PHYS_CMD_SET_STATE = 5
} phys_cmd_type_t;

/** Shape tag for spawn commands. */
typedef enum phys_cmd_shape {
    PHYS_CMD_SHAPE_BOX     = 0,
    PHYS_CMD_SHAPE_SPHERE  = 1,
    PHYS_CMD_SHAPE_CAPSULE = 2
} phys_cmd_shape_t;

/** Spawn a new rigid body. */
typedef struct phys_cmd_spawn_body {
    phys_vec3_t position;        /**< World-space position. */
    phys_quat_t orientation;     /**< Initial orientation. */
    phys_vec3_t linear_vel;      /**< Initial linear velocity. */
    float       mass;            /**< Mass in kg.  0 = static. */
    uint32_t    flags;           /**< PHYS_BODY_FLAG_* bitmask. */

    phys_cmd_shape_t shape;      /**< Collider shape type. */
    union {
        phys_vec3_t box_half;    /**< Half-extents for box. */
        float       sphere_r;   /**< Radius for sphere. */
        struct {
            float radius;        /**< Capsule radius. */
            float half_height;   /**< Capsule half-height. */
        } capsule;
    } shape_data;

    /** Opaque user data passed back via the result callback.
     *  Useful for mapping the created body index to game-level metadata. */
    uint64_t user_tag;
} phys_cmd_spawn_body_t;

/** Set a body's position (kinematic teleport). */
typedef struct phys_cmd_set_position {
    uint32_t    body_index;      /**< Target body. */
    phys_vec3_t position;        /**< New position. */
} phys_cmd_set_position_t;

/** Apply an impulse to a body. */
typedef struct phys_cmd_apply_impulse {
    uint32_t    body_index;      /**< Target body. */
    phys_vec3_t impulse;         /**< Impulse vector (kg·m/s). */
} phys_cmd_apply_impulse_t;

/** Destroy a body by index. */
typedef struct phys_cmd_destroy_body {
    uint32_t body_index;         /**< Body to destroy. */
} phys_cmd_destroy_body_t;

/** Set a body's full authoritative state. */
typedef struct phys_cmd_set_state {
    uint32_t    body_index;      /**< Target body. */
    phys_vec3_t position;        /**< New position. */
    phys_quat_t orientation;     /**< New orientation. */
    phys_vec3_t linear_vel;      /**< New linear velocity. */
} phys_cmd_set_state_t;

/**
 * @brief Callback invoked for each SPAWN_BODY command after the body
 *        is created, allowing the caller to record the mapping between
 *        user_tag and the assigned body_index.
 *
 * @param body_index  The physics body index assigned (UINT32_MAX on failure).
 * @param user_tag    The user_tag from the spawn command.
 * @param user        Opaque context pointer.
 */
typedef void (*phys_cmd_spawn_callback_t)(uint32_t body_index,
                                           uint64_t user_tag,
                                           void *user);

/* Forward declarations to avoid pulling in full headers. */
struct phys_world;
struct fr_topic_channel;

/**
 * @brief Drain all pending commands from a topic channel and apply them
 *        to the physics world.
 *
 * Called at the start of each physics tick, before any simulation stages.
 * Commands are deserialized from the channel's byte ring buffer.
 *
 * @param world          Physics world to mutate.
 * @param cmd_channel    Topic channel containing serialized commands.
 * @param spawn_cb       Optional callback invoked for each SPAWN_BODY result.
 * @param spawn_cb_user  Opaque pointer forwarded to spawn_cb.
 *
 * Thread safety: must be called from a single thread/fiber (the tick fiber).
 */
void phys_cmd_drain(struct phys_world *world,
                    struct fr_topic_channel *cmd_channel,
                    phys_cmd_spawn_callback_t spawn_cb,
                    void *spawn_cb_user);

/**
 * @brief Push a serialized physics command into a topic channel.
 *
 * Convenience wrapper that prepends the 1-byte type tag.
 *
 * @param cmd_channel  Topic channel to push into.
 * @param type         Command type tag.
 * @param payload      Command struct (e.g., phys_cmd_spawn_body_t).
 * @param payload_size Size of the command struct in bytes.
 * @return true on success, false if the channel is full.
 *
 * Thread safety: may be called from any thread; the topic channel's
 * internal spinlock provides synchronization.
 */
bool phys_cmd_push(struct fr_topic_channel *cmd_channel,
                   phys_cmd_type_t type,
                   const void *payload, size_t payload_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_PHYS_CMD_H */

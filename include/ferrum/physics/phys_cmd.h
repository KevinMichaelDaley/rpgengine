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
    PHYS_CMD_SET_STATE = 5,

    /** Add a joint between two bodies.
     *  Payload: phys_cmd_add_joint_t. */
    PHYS_CMD_ADD_JOINT = 6,

    /** Set a body's friction and restitution.
     *  Payload: phys_cmd_set_material_t. */
    PHYS_CMD_SET_MATERIAL = 7
} phys_cmd_type_t;

/** Shape tag for spawn commands. */
typedef enum phys_cmd_shape {
    PHYS_CMD_SHAPE_BOX       = 0,
    PHYS_CMD_SHAPE_SPHERE    = 1,
    PHYS_CMD_SHAPE_CAPSULE   = 2,
    PHYS_CMD_SHAPE_HALFSPACE = 3
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
        struct {
            phys_vec3_t normal;  /**< Outward-facing unit normal. */
            float       distance; /**< Signed distance from origin. */
        } halfspace;
    } shape_data;

    float       friction;        /**< Surface friction (0 = use default 0.5). */
    float       restitution;     /**< Bounciness (0 = use default 0.0). */
    /** If true, friction/restitution values override body_init defaults. */
    bool        has_material;

    /** Physics tier override (0 = default/ANIM, 1–3 = DIRECT/NEAR/VISIBLE).
     *  When non-zero, the spawned body's tier is set to this value. */
    uint8_t tier;

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

/** Bitmask for phys_cmd_set_state_t::flags — which fields to apply. */
enum {
    PHYS_SET_POS     = 1u << 0, /**< Apply position. */
    PHYS_SET_ORI     = 1u << 1, /**< Apply orientation. */
    PHYS_SET_LIN_VEL = 1u << 2, /**< Apply linear velocity. */
    PHYS_SET_ANG_VEL = 1u << 3, /**< Apply angular velocity. */
    /** Apply all fields (backwards-compatible default). */
    PHYS_SET_ALL     = PHYS_SET_POS | PHYS_SET_ORI |
                       PHYS_SET_LIN_VEL | PHYS_SET_ANG_VEL
};

/** Set body state — only fields enabled in `flags` are written. */
typedef struct phys_cmd_set_state {
    uint32_t    body_index;      /**< Target body. */
    uint32_t    flags;           /**< Bitmask of PHYS_SET_* fields to apply. */
    phys_vec3_t position;        /**< New position (if PHYS_SET_POS). */
    phys_quat_t orientation;     /**< New orientation (if PHYS_SET_ORI). */
    phys_vec3_t linear_vel;      /**< New linear velocity (if PHYS_SET_LIN_VEL). */
    phys_vec3_t angular_vel;     /**< New angular velocity (if PHYS_SET_ANG_VEL). */
} phys_cmd_set_state_t;

/** Add a joint constraint between two bodies. */
typedef struct phys_cmd_add_joint {
    uint32_t    body_a;          /**< First body index. */
    uint32_t    body_b;          /**< Second body index. */
    int         joint_type;      /**< 0=distance, 1=ball, 2=hinge. */
    phys_vec3_t local_anchor_a;  /**< Anchor in body A local space. */
    phys_vec3_t local_anchor_b;  /**< Anchor in body B local space. */
    phys_vec3_t axis;            /**< Joint axis (hinge only). */
} phys_cmd_add_joint_t;

/** Set a body's surface material properties (friction and restitution). */
typedef struct phys_cmd_set_material {
    uint32_t body_index;         /**< Target body. */
    float    friction;           /**< Surface friction coefficient (0–1+). */
    float    restitution;        /**< Coefficient of restitution (0–1). */
} phys_cmd_set_material_t;

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
struct phys_body;
struct fr_topic_channel;

/**
 * @brief Staging buffer for deferred body mutations.
 *
 * Commands are stored as contiguous [type_byte | payload] entries.
 * Populated by phys_cmd_drain_spawns(), consumed by
 * phys_cmd_apply_mutations().
 */
typedef struct phys_cmd_mutation_list {
    uint8_t *data;    /**< Pre-allocated buffer for staged commands. */
    size_t   used;    /**< Bytes currently used. */
    size_t   cap;     /**< Total buffer capacity in bytes. */
} phys_cmd_mutation_list_t;

/**
 * @brief Drain all pending commands from a topic channel and apply them
 *        to the physics world.
 *
 * Called while paused to process commands without a physics tick.
 * NOT safe to call concurrently with network reads of bodies_curr
 * for mutation commands — use phys_cmd_drain_spawns() +
 * phys_cmd_apply_mutations() for thread-safe operation during ticks.
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
 * @brief Drain commands, splitting spawns from mutations.
 *
 * Spawn/destroy/joint commands are applied immediately to the world
 * (writing to bodies_curr — safe because new body slots don't
 * contend with existing network reads of active bodies).
 *
 * Mutation commands (SET_POSITION, APPLY_IMPULSE, SET_STATE,
 * SET_MATERIAL) are copied into the deferred list for later
 * application to bodies_next before the buffer swap.
 *
 * @param world          Physics world.
 * @param cmd_channel    Topic channel to drain.
 * @param spawn_cb       Callback for spawns (may be NULL).
 * @param spawn_cb_user  Opaque pointer for spawn_cb.
 * @param deferred       Output list for mutations.  Must be initialized
 *                       with .data pointing to a pre-allocated buffer
 *                       and .cap set.  .used is reset to 0 on entry.
 */
void phys_cmd_drain_spawns(struct phys_world *world,
                           struct fr_topic_channel *cmd_channel,
                           phys_cmd_spawn_callback_t spawn_cb,
                           void *spawn_cb_user,
                           phys_cmd_mutation_list_t *deferred);

/**
 * @brief Apply deferred mutations to a target body buffer.
 *
 * Processes SET_POSITION, APPLY_IMPULSE, SET_STATE, and SET_MATERIAL
 * commands from the deferred list, writing to the target buffer
 * (typically bodies_next) rather than bodies_curr.
 *
 * @param mutations      Deferred mutation list from phys_cmd_drain_spawns().
 * @param target_bodies  Body buffer to write to (e.g., bodies_next).
 * @param body_cap       Capacity of the target buffer (for bounds check).
 */
void phys_cmd_apply_mutations(const phys_cmd_mutation_list_t *mutations,
                              struct phys_body *target_bodies,
                              uint32_t body_cap);

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

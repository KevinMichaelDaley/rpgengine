#ifndef FERRUM_PHYSICS_WORLD_H
#define FERRUM_PHYSICS_WORLD_H

/** @file
 * @brief Top-level physics world container.
 *
 * Owns body pools, colliders, shape arrays, manifold cache, and
 * per-frame arena.  Provides the main interface for creating and
 * destroying rigid bodies and attaching collider shapes.
 */

#include "ferrum/physics/phys_types.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/aabb.h"
#include "ferrum/physics/manifold_cache.h"
#include "ferrum/physics/spatial_grid.h"
#include "ferrum/physics/static_bvh.h"
#include "ferrum/physics/joint.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration for impact event type (defined in cache_commit.h). */
struct phys_impact_event;

/* ── Configuration ──────────────────────────────────────────────── */

/**
 * @brief Physics world configuration.
 *
 * Controls pool sizes, solver parameters, and sleep thresholds.
 * Obtain reasonable defaults with phys_world_config_default().
 */
typedef struct phys_world_config {
    uint32_t max_bodies;               /**< Maximum number of rigid bodies. */
    uint32_t max_colliders;            /**< Maximum number of colliders. */
    uint32_t manifold_cache_size;      /**< Manifold cache capacity. */
    size_t   frame_arena_size;         /**< Per-frame arena size in bytes. */
    float    fixed_dt;                 /**< Fixed timestep (seconds). */
    phys_vec3_t gravity;               /**< Gravity vector. */
    uint32_t default_substeps;         /**< Physics substeps per tick. */
    uint32_t default_solver_iterations;/**< Constraint solver iterations. */
    float    baumgarte;                /**< Baumgarte stabilisation factor. */
    float    slop;                     /**< Penetration slop. */
    float    sleep_threshold_linear;   /**< Linear velocity sleep threshold. */
    float    sleep_threshold_angular;  /**< Angular velocity sleep threshold. */
    uint32_t sleep_delay_frames;       /**< Frames below threshold before sleep. */
    float    warmstart_decay;          /**< Impulse decay per cache commit (0-1). */
    float    velocity_damping;         /**< Velocity fraction retained per second (0-1). */
    uint32_t island_color_threshold;   /**< Min constraints per island for graph coloring (0 = disabled). */
    float    speculative_margin;       /**< Max separation for speculative contacts (0 = disabled). */
    uint32_t max_island_bodies;        /**< Max bodies per island for splitting (0 = unlimited). */
    uint32_t max_joints;               /**< Maximum number of joints. */
    float    max_dt_override;          /**< Max dt when using variable timestep (multiplier of fixed_dt, default 3.0). */
} phys_world_config_t;

/* ── World container ────────────────────────────────────────────── */

/**
 * @brief Top-level physics world container.
 *
 * Owns all simulation state.  Caller must call phys_world_init()
 * before use and phys_world_destroy() when done.
 *
 * Ownership: the world owns all internal arrays and sub-structures.
 * Pointers returned by get_body / get_collider / get_aabb are owned
 * by the world and are invalidated by destroy.
 */
typedef struct phys_world {
    phys_world_config_t config;

    /** When true, the tick skips narrowphase collision response (stages
     *  6–11: narrowphase, manifold, stabilization, constraints, islands,
     *  TGS solve).  Bodies still integrate under gravity and broadphase
     *  still runs.  Used on the client for server-authoritative
     *  prediction — the server sends corrections for colliding bodies. */
    uint8_t prediction_mode;

    /* Double-buffered body pool. */
    phys_body_pool_t body_pool;

    /* Per-body colliders and AABBs (indexed by body pool slot). */
    phys_collider_t *colliders;  /**< Array of size max_bodies. */
    phys_aabb_t     *aabbs;      /**< Array of size max_bodies. */

    /* Shape storage (indexed by collider.shape_index). */
    phys_sphere_t  *spheres;     /**< Sphere shapes array. */
    phys_box_t     *boxes;       /**< Box shapes array. */
    phys_capsule_t *capsules;    /**< Capsule shapes array. */
    uint32_t sphere_count;       /**< Number of allocated spheres. */
    uint32_t box_count;          /**< Number of allocated boxes. */
    uint32_t capsule_count;      /**< Number of allocated capsules. */

    /* Persistent manifold cache. */
    phys_manifold_cache_t manifold_cache;

    /* Per-frame arena. */
    phys_frame_arena_t frame_arena;

    /* Persistent static-geometry BVH (built once, used every tick). */
    phys_static_bvh_t static_bvh;
    phys_frame_arena_t static_bvh_arena;
    uint8_t static_bvh_valid;
    uint8_t *static_bucket_flags;
    uint32_t static_bucket_flag_count;

    /* Cached spatial grid for queries (valid when query_grid_valid != 0). */
    phys_spatial_grid_t query_grid;
    uint8_t query_grid_valid;

    /* Tick counter. */
    uint64_t tick_count;

    /** When positive, overrides fixed_dt for the next tick.  Set by
     *  the tick runner when sustained overload is detected.  Clamped to
     *  config.max_dt_override × fixed_dt.  Reset to 0 when performance
     *  recovers.  Written only by the tick thread. */
    float dt_override;

    /** Timestamp (nanoseconds, CLOCK_MONOTONIC) of the last tick
     *  completion.  Used to enforce a minimum wall-clock interval
     *  between ticks so the simulation runs at a fixed rate. */
    uint64_t last_tick_ns;

    /* Impact event buffer (filled by cache_commit stage). */
    struct phys_impact_event *impact_events;   /**< Dynamically allocated event buffer. */
    uint32_t impact_event_count;               /**< Current number of events this frame. */
    uint32_t impact_event_capacity;            /**< Allocated capacity. */
    float    impact_threshold;                 /**< Minimum impulse to emit event. */

    /* Joint array (persistent, not per-frame). */
    phys_joint_t *joints;                      /**< Array of active joints. */
    uint32_t joint_count;                      /**< Number of active joints. */
    uint32_t joint_capacity;                   /**< Allocated joint capacity. */
} phys_world_t;

/* ── Configuration API ──────────────────────────────────────────── */

/**
 * @brief Return a default world configuration with reasonable values.
 * @return Default configuration struct.
 */
phys_world_config_t phys_world_config_default(void);

/* ── Lifecycle API ──────────────────────────────────────────────── */

/**
 * @brief Initialize a physics world from configuration.
 *
 * Allocates body pool, collider/AABB arrays, shape arrays, manifold
 * cache, and frame arena.
 *
 * @param world  World to initialize (non-NULL).
 * @param config Configuration (non-NULL).
 * @return 0 on success, -1 on failure (NULL args or allocation failure).
 *
 * Ownership: caller owns the world and must call phys_world_destroy().
 * Side effects: allocates heap memory.
 */
int phys_world_init(phys_world_t *world, const phys_world_config_t *config);

/**
 * @brief Destroy a physics world and free all memory.
 *
 * @param world  World to destroy (NULL-safe, no-op if NULL).
 *
 * Side effects: frees all internal memory; zeroes the struct.
 */
void phys_world_destroy(phys_world_t *world);

/* ── Static BVH management API ──────────────────────────────────── */

/**
 * @brief Mark the persistent static BVH as invalid.
 *
 * The static BVH is rebuilt lazily on the next tick (after AABB update).
 * Call this when static bodies are added/removed or when their collider
 * geometry changes.
 *
 * @param world World (NULL-safe, no-op if NULL).
 */
void phys_world_static_bvh_invalidate(phys_world_t *world);

/* ── Body management API ────────────────────────────────────────── */

/**
 * @brief Create a new rigid body in the world.
 *
 * The body is initialized with safe defaults (static, identity
 * orientation, zero velocity).
 *
 * @param world  World (non-NULL).
 * @return Body index, or UINT32_MAX on failure (NULL world or pool full).
 *
 * Ownership: the world owns the body.
 */
uint32_t phys_world_create_body(phys_world_t *world);

/**
 * @brief Remove a body from the world.
 *
 * Marks the body slot inactive.  The collider and AABB at that index
 * remain allocated but become inactive.
 *
 * @param world  World (NULL-safe, no-op if NULL).
 * @param index  Body index (out-of-range is a no-op).
 */
void phys_world_destroy_body(phys_world_t *world, uint32_t index);

/**
 * @brief Get a mutable pointer to a body by index.
 *
 * @param world  World (NULL returns NULL).
 * @param index  Body index.
 * @return Pointer to the body in the current-frame buffer, or NULL
 *         if inactive / out-of-range / NULL world.
 *
 * Ownership: the returned pointer is owned by the world.
 */
phys_body_t *phys_world_get_body(phys_world_t *world, uint32_t index);

/**
 * @brief Return the number of active bodies.
 *
 * @param world  World (NULL returns 0).
 * @return Active body count.
 */
uint32_t phys_world_body_count(const phys_world_t *world);

/* ── Collider management API ────────────────────────────────────── */

/**
 * @brief Attach a sphere collider to a body.
 *
 * Allocates a sphere shape and sets up the collider reference.
 *
 * @param world       World (NULL-safe, no-op if NULL).
 * @param body_index  Body index.
 * @param radius      Sphere radius.
 * @param offset      Local offset from body origin.
 *
 * Side effects: increments sphere_count; writes colliders[body_index].
 */
void phys_world_set_sphere_collider(phys_world_t *world, uint32_t body_index,
                                    float radius, phys_vec3_t offset);

/**
 * @brief Attach a box collider to a body.
 *
 * Allocates a box shape and sets up the collider reference.
 *
 * @param world        World (NULL-safe, no-op if NULL).
 * @param body_index   Body index.
 * @param half_extents Box half-extents along local axes.
 * @param offset       Local offset from body origin.
 * @param rotation     Local rotation relative to body.
 *
 * Side effects: increments box_count; writes colliders[body_index].
 */
void phys_world_set_box_collider(phys_world_t *world, uint32_t body_index,
                                 phys_vec3_t half_extents, phys_vec3_t offset,
                                 phys_quat_t rotation);

/**
 * @brief Attach a capsule collider to a body.
 *
 * Allocates a capsule shape and sets up the collider reference.
 *
 * @param world       World (NULL-safe, no-op if NULL).
 * @param body_index  Body index.
 * @param radius      Capsule radius.
 * @param half_height Half of the cylinder segment length.
 * @param offset      Local offset from body origin.
 * @param rotation    Local rotation relative to body.
 *
 * Side effects: increments capsule_count; writes colliders[body_index].
 */
void phys_world_set_capsule_collider(phys_world_t *world, uint32_t body_index,
                                     float radius, float half_height,
                                     phys_vec3_t offset, phys_quat_t rotation);

/**
 * @brief Get a read-only pointer to a body's collider.
 *
 * @param world       World (NULL returns NULL).
 * @param body_index  Body index.
 * @return Pointer to the collider, or NULL if body is inactive,
 *         out-of-range, or world is NULL.
 *
 * Ownership: the returned pointer is owned by the world.
 */
const phys_collider_t *phys_world_get_collider(const phys_world_t *world,
                                               uint32_t body_index);

/* ── Query API ──────────────────────────────────────────────────── */

/**
 * @brief Get a read-only pointer to a body's AABB.
 *
 * @param world       World (NULL returns NULL).
 * @param body_index  Body index.
 * @return Pointer to the AABB, or NULL if body is inactive,
 *         out-of-range, or world is NULL.
 *
 * Ownership: the returned pointer is owned by the world.
 */
const phys_aabb_t *phys_world_get_aabb(const phys_world_t *world,
                                       uint32_t body_index);

/**
 * @brief Return the current tick count.
 *
 * @param world  World (NULL returns 0).
 * @return Number of simulation ticks executed so far.
 */
uint64_t phys_world_tick_count(const phys_world_t *world);

/* ── Impact Event API ───────────────────────────────────────────── */

/**
 * @brief Get a read-only pointer to the impact event buffer.
 *
 * @param world     World (NULL returns NULL, sets *out_count to 0).
 * @param out_count Output: number of events (may be NULL).
 * @return Pointer to the event array, owned by the world.
 */
const struct phys_impact_event *phys_world_get_impact_events(
    const phys_world_t *world, uint32_t *out_count);

/**
 * @brief Clear all impact events (sets count to 0).
 *
 * @param world  World (NULL-safe, no-op if NULL).
 */
void phys_world_clear_impact_events(phys_world_t *world);

/**
 * @brief Copy impact events involving a specific body into an output buffer.
 *
 * @param world      World (NULL returns 0).
 * @param body_idx   Body index to filter by (matches body_a or body_b).
 * @param out_events Output buffer (non-NULL).
 * @param max_events Capacity of out_events.
 * @return Number of events copied.
 */
uint32_t phys_world_get_impact_events_for_body(
    const phys_world_t *world, uint32_t body_idx,
    struct phys_impact_event *out_events, uint32_t max_events);

/**
 * @brief Find the strongest impact event for a specific body.
 *
 * @param world     World (NULL returns false).
 * @param body_idx  Body index to filter by.
 * @param out_event Output: the event with highest impulse (non-NULL).
 * @return true if an event was found, false otherwise.
 */
bool phys_world_get_strongest_impact(
    const phys_world_t *world, uint32_t body_idx,
    struct phys_impact_event *out_event);

/**
 * @brief Set the minimum impulse magnitude required to emit impact events.
 *
 * @param world     World (NULL-safe, no-op if NULL).
 * @param threshold Threshold value (negative values clamped to 0).
 */
void phys_world_set_impact_threshold(phys_world_t *world, float threshold);

/**
 * @brief Get the current impact event threshold.
 *
 * @param world  World (NULL returns 0.0f).
 * @return Current threshold value.
 */
float phys_world_get_impact_threshold(const phys_world_t *world);

/* ── Joint management API ───────────────────────────────────────── */

/**
 * @brief Add a joint to the world.
 *
 * The joint is copied into the world's joint array.  The caller should
 * have already set type, body indices, anchors, and parameters.
 *
 * @param world  World (non-NULL).
 * @param joint  Joint to add (non-NULL, fully configured).
 * @return Joint index, or UINT32_MAX on failure (NULL args, capacity full,
 *         or invalid body indices).
 *
 * Ownership: the world owns the copied joint data.
 */
uint32_t phys_world_add_joint(phys_world_t *world, const phys_joint_t *joint);

/**
 * @brief Remove a joint from the world by index.
 *
 * Swap-removes the joint at the given index.  Joint indices may change
 * after removal.
 *
 * @param world  World (NULL-safe, no-op if NULL).
 * @param index  Joint index (out-of-range is a no-op).
 */
void phys_world_remove_joint(phys_world_t *world, uint32_t index);

/**
 * @brief Get a mutable pointer to a joint by index.
 *
 * @param world  World (NULL returns NULL).
 * @param index  Joint index.
 * @return Pointer to joint, or NULL if out-of-range / NULL world.
 *
 * Ownership: the returned pointer is owned by the world.
 */
phys_joint_t *phys_world_get_joint(phys_world_t *world, uint32_t index);

/**
 * @brief Return the number of active joints.
 *
 * @param world  World (NULL returns 0).
 * @return Active joint count.
 */
uint32_t phys_world_joint_count(const phys_world_t *world);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_WORLD_H */

/**
 * @file priority_body_sender.h
 * @brief Velocity-proportional priority BODY_STATE sender.
 *
 * Sends per-body state updates at a rate proportional to each body's
 * speed.  Fast-moving constrained bodies get near-physics-rate updates;
 * slow or sleeping ones get nothing (snapshots suffice).
 *
 * Designed to be called from the physics thread's post-tick callback
 * at full physics tick rate (~60Hz).  Internally rate-limits per body.
 *
 * Types: fr_priority_body_sender_t, fr_priority_body_sender_config_t (2 types).
 */
#ifndef FERRUM_SERVER_PHYSICS_NET_PRIORITY_BODY_SENDER_H
#define FERRUM_SERVER_PHYSICS_NET_PRIORITY_BODY_SENDER_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/net/udp_socket.h"

#ifdef __cplusplus
extern "C" {
#endif

struct phys_world;

/**
 * @brief Configuration for the priority body sender.
 */
typedef struct fr_priority_body_sender_config {
    uint32_t max_bodies;     /**< Max body pool capacity. */

    /** Speed (m/s) at which a body gets max update rate.
     *  Bodies at or above this speed send every physics tick.
     *  Default suggestion: 10.0 m/s. */
    float    speed_full_rate;

    /** Speed (m/s) below which no priority updates are sent.
     *  Default suggestion: 0.3 m/s. */
    float    speed_min;

    /** Maximum ticks between updates for a moving body.
     *  Bodies just above speed_min send at this interval.
     *  Default suggestion: 30 (= 0.5s at 60Hz physics). */
    uint32_t max_interval;
} fr_priority_body_sender_config_t;

/**
 * @brief Opaque priority body sender.
 *
 * Owns per-body tick counters for rate limiting.
 */
typedef struct fr_priority_body_sender fr_priority_body_sender_t;

/**
 * @brief Create a priority body sender.
 *
 * @param cfg Configuration (non-NULL).
 * @return Allocated sender, or NULL on failure.
 * Ownership: caller owns the returned pointer.
 */
fr_priority_body_sender_t *fr_priority_body_sender_create(
    const fr_priority_body_sender_config_t *cfg);

/**
 * @brief Destroy a priority body sender.
 * Safe to call with NULL.
 */
void fr_priority_body_sender_destroy(fr_priority_body_sender_t *s);

/**
 * @brief Tick the sender: encode and send priority updates.
 *
 * Iterates all active bodies in the world.  For each body flagged in
 * `constrained_flags`, computes a velocity-proportional send interval
 * and sends a raw UDP BODY_STATE datagram if the body is due.
 *
 * When joint pairs are provided, bodies sharing a joint are promoted
 * to the faster partner's send rate so both sides of the constraint
 * stay in sync on the client.
 *
 * @param s                 Sender (non-NULL).
 * @param world             Physics world to read from (non-NULL).
 * @param constrained_flags Per-body uint8 array [max_bodies]; 1 = constrained.
 * @param tick              Current physics tick number.
 * @param sock              Raw UDP socket for sending (non-NULL).
 * @param addrs             Array of client addresses.
 * @param addr_active       Per-client active flags (1 = connected).
 * @param max_clients       Length of addrs / addr_active arrays.
 * @param joint_pairs       Flat array of body index pairs [a0,b0,a1,b1,...].
 *                          May be NULL if joint_pair_count is 0.
 * @param joint_pair_count  Number of joint pairs (half the array length).
 * @return Number of body updates sent this tick.
 *
 * Ownership: borrows all pointers.
 * Side effects: sends UDP datagrams, updates internal tick counters.
 */
uint32_t fr_priority_body_sender_tick(
    fr_priority_body_sender_t *s,
    const struct phys_world *world,
    const uint8_t *constrained_flags,
    uint64_t tick,
    net_udp_socket_t *sock,
    const net_udp_addr_t *addrs,
    const uint8_t *addr_active,
    uint16_t max_clients,
    const uint32_t *joint_pairs,
    uint32_t joint_pair_count);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_SERVER_PHYSICS_NET_PRIORITY_BODY_SENDER_H */

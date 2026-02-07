#ifndef FERRUM_DEMO_INPUT_SPAWN_H
#define FERRUM_DEMO_INPUT_SPAWN_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/replication/common.h"

/** @file
 * @brief Demo input: spawn a box.
 *
 * Client -> Server (reliable). 12 bytes on wire.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define DEMO_INPUT_SPAWN_PAYLOAD_SIZE 12u

typedef struct demo_input_spawn {
    uint16_t event_id;
    uint16_t half_x_mm;   /* half-extent X in millimeters */
    uint16_t half_y_mm;   /* half-extent Y in millimeters */
    uint16_t half_z_mm;   /* half-extent Z in millimeters */
    uint32_t color_seed;  /* hashed for RGB */
} demo_input_spawn_t;

/**
 * @brief Encode a demo_input_spawn_t into a big-endian wire payload.
 *
 * @param msg       Source message. Must not be NULL.
 * @param out       Destination buffer. Must not be NULL.
 * @param out_size  Size of @p out in bytes. Must be >= DEMO_INPUT_SPAWN_PAYLOAD_SIZE.
 * @return NET_REPL_OK on success, NET_REPL_ERR_INVALID if NULL, NET_REPL_ERR_SHORT if too small.
 */
int demo_input_spawn_encode(const demo_input_spawn_t *msg, uint8_t *out, size_t out_size);

/**
 * @brief Decode a big-endian wire payload into a demo_input_spawn_t.
 *
 * @param msg       Destination message. Must not be NULL.
 * @param payload   Source buffer. Must not be NULL.
 * @param size      Size of @p payload in bytes. Must be >= DEMO_INPUT_SPAWN_PAYLOAD_SIZE.
 * @return NET_REPL_OK on success, NET_REPL_ERR_INVALID if NULL, NET_REPL_ERR_SHORT if too small.
 */
int demo_input_spawn_decode(demo_input_spawn_t *msg, const uint8_t *payload, size_t size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_DEMO_INPUT_SPAWN_H */

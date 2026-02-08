#ifndef FERRUM_DEMO_INPUT_MOVE_H
#define FERRUM_DEMO_INPUT_MOVE_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/replication/common.h"

/** @file
 * @brief Demo input: player movement + actions.
 *
 * Client -> Server (unreliable). 8 bytes on wire.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define DEMO_INPUT_MOVE_PAYLOAD_SIZE 8u

/* Move flag bits. */
#define DEMO_MOVE_W    (1u << 0)
#define DEMO_MOVE_A    (1u << 1)
#define DEMO_MOVE_S    (1u << 2)
#define DEMO_MOVE_D    (1u << 3)

/* Action flag bits. */
#define DEMO_ACTION_FIRE      (1u << 0)
#define DEMO_ACTION_SPAWN_BOX (1u << 1)

typedef struct demo_input_move {
    uint16_t event_id;
    int16_t  yaw_snorm16;    /* look yaw quantized to [-32767, 32767] */
    int16_t  pitch_snorm16;  /* look pitch quantized */
    uint8_t  move_flags;     /* DEMO_MOVE_W/A/S/D bits */
    uint8_t  action_flags;   /* DEMO_ACTION_FIRE/SPAWN_BOX bits */
} demo_input_move_t;

/**
 * @brief Encode a demo_input_move_t into a big-endian wire payload.
 *
 * @param msg       Source message. Must not be NULL.
 * @param out       Destination buffer. Must not be NULL.
 * @param out_size  Size of @p out in bytes. Must be >= DEMO_INPUT_MOVE_PAYLOAD_SIZE.
 * @return NET_REPL_OK on success, NET_REPL_ERR_INVALID if NULL, NET_REPL_ERR_SHORT if too small.
 */
int demo_input_move_encode(const demo_input_move_t *msg, uint8_t *out, size_t out_size);

/**
 * @brief Decode a big-endian wire payload into a demo_input_move_t.
 *
 * @param msg       Destination message. Must not be NULL.
 * @param payload   Source buffer. Must not be NULL.
 * @param size      Size of @p payload in bytes. Must be >= DEMO_INPUT_MOVE_PAYLOAD_SIZE.
 * @return NET_REPL_OK on success, NET_REPL_ERR_INVALID if NULL, NET_REPL_ERR_SHORT if too small.
 */
int demo_input_move_decode(demo_input_move_t *msg, const uint8_t *payload, size_t size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_DEMO_INPUT_MOVE_H */

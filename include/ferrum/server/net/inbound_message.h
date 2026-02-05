#ifndef FERRUM_SERVER_NET_INBOUND_MESSAGE_H
#define FERRUM_SERVER_NET_INBOUND_MESSAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** \file
 * \brief Encoding/decoding helpers for server runtime inbound topic messages.
 *
 * These helpers formalize the byte layout used by `fr_server_net_runtime_t` for
 * publishing decoded inbound messages to a topic channel.
 */

/** View into a decoded inbound message.
 *
 * Ownership: `payload` points into the caller-provided message buffer.
 */
typedef struct fr_server_net_inbound_message_view_t {
    uint16_t client_id;
    uint16_t schema_id;
    bool reliable;
    const uint8_t *payload;
    size_t payload_size;
} fr_server_net_inbound_message_view_t;

/** Encode an inbound message into a byte buffer.
 *
 * Layout:
 * - client_id: u16 LE
 * - schema_id: u16 LE
 * - flags: u8 (bit0=reliable)
 * - reserved: u8 (0)
 * - payload: bytes
 *
 * \param client_id Client index/ID.
 * \param reliable Whether the message was received as reliable.
 * \param schema_id Schema/message ID.
 * \param payload Optional payload bytes; may be NULL when payload_size==0.
 * \param payload_size Payload length in bytes.
 * \param out Output buffer.
 * \param out_cap Output buffer capacity.
 * \param out_size Output: written size on success.
 * \return true on success; false on invalid args or insufficient capacity.
 */
bool fr_server_net_inbound_message_encode(uint16_t client_id,
                                         bool reliable,
                                         uint16_t schema_id,
                                         const void *payload,
                                         size_t payload_size,
                                         uint8_t *out,
                                         size_t out_cap,
                                         size_t *out_size);

/** Decode an inbound message buffer into a view.
 *
 * \param out View to fill.
 * \param msg Message bytes.
 * \param msg_size Message size in bytes.
 * \return true on success; false on invalid/short buffers.
 */
bool fr_server_net_inbound_message_decode(fr_server_net_inbound_message_view_t *out,
                                         const uint8_t *msg,
                                         size_t msg_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_SERVER_NET_INBOUND_MESSAGE_H */

/**
 * @file client_state_dispatch.h
 * @brief Dispatch incoming JSON messages on the client state socket.
 *
 * Parses newline-delimited JSON lines received from the controller and
 * routes them to the appropriate handler (cursor, camera, selection, grab).
 * Also provides helpers for building push-event JSON.
 *
 * Thread safety: single-threaded (client main thread only).
 */
#ifndef FERRUM_EDITOR_CLIENT_STATE_DISPATCH_H
#define FERRUM_EDITOR_CLIENT_STATE_DISPATCH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* Forward declarations. */
struct client_state_socket;
struct editor_cursor;

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief Client-side dispatch context.
 *
 * Holds pointers to client state objects that can be queried/commanded
 * by the controller, and the socket for sending push events.
 */
typedef struct client_state_dispatch {
    struct client_state_socket *socket;   /**< State socket for send/recv. */
    struct editor_cursor       *cursor;   /**< 3D cursor state (non-NULL). */
    /* Future: camera, selection, grab state pointers. */
} client_state_dispatch_t;

/* ------------------------------------------------------------------------ */
/* Core dispatch                                                             */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize dispatch context.
 * @param disp    Dispatch context (non-NULL).
 * @param socket  State socket (non-NULL).
 * @param cursor  Cursor state (non-NULL).
 */
void client_state_dispatch_init(client_state_dispatch_t *disp,
                                 struct client_state_socket *socket,
                                 struct editor_cursor *cursor);

/**
 * @brief Process all pending lines from the state socket.
 *
 * Pops each complete line, parses JSON, and routes to the appropriate
 * handler. Query responses are sent back on the socket.
 *
 * Call once per frame after client_state_socket_poll().
 *
 * @param disp  Dispatch context (non-NULL).
 * @return Number of messages processed.
 */
uint32_t client_state_dispatch_drain(client_state_dispatch_t *disp);

/* ------------------------------------------------------------------------ */
/* Push events (client → controller)                                         */
/* ------------------------------------------------------------------------ */

/**
 * @brief Push a cursor_moved event to the controller.
 * @param disp  Dispatch context.
 * @param x, y, z  New cursor world position.
 */
void client_state_push_cursor_moved(client_state_dispatch_t *disp,
                                     float x, float y, float z);

/**
 * @brief Push an entity_clicked event to the controller.
 * @param disp       Dispatch context.
 * @param entity_id  ID of the clicked entity.
 * @param x, y, z    Click world position.
 */
void client_state_push_entity_clicked(client_state_dispatch_t *disp,
                                       uint32_t entity_id,
                                       float x, float y, float z);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_CLIENT_STATE_DISPATCH_H */

/**
 * @file client_state_socket.h
 * @brief Client-side TCP state socket for editor controller communication.
 *
 * The client listens on a configurable TCP port. The controller connects
 * and exchanges newline-delimited JSON messages:
 *
 * - Controller → Client: queries and commands (cursor, camera, grab)
 * - Client → Controller: push events (clicks, box-select, context menu)
 *
 * Single client connection at a time. Non-blocking I/O — never stalls
 * the render loop. Call poll() each frame.
 *
 * Only compiled when EDITOR_ENABLE is defined.
 *
 * Thread safety: single-threaded (client main/render thread only).
 */
#ifndef FERRUM_EDITOR_CLIENT_STATE_SOCKET_H
#define FERRUM_EDITOR_CLIENT_STATE_SOCKET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------------ */
/* Constants                                                                 */
/* ------------------------------------------------------------------------ */

/** Default TCP port for the client state socket. */
#define CLIENT_STATE_PORT_DEFAULT 9200

/** Receive buffer size (bytes). */
#define CLIENT_STATE_RECV_BUF 8192

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief Client-side TCP state socket.
 *
 * Listens for a single controller connection. Accumulates received data
 * in recv_buf; use pop_line() to extract complete newline-delimited messages.
 *
 * Ownership: listen() opens the listener; destroy() closes all fds.
 */
typedef struct client_state_socket {
    int      listen_fd;   /**< Listening socket fd (-1 = not listening). */
    int      client_fd;   /**< Connected controller fd (-1 = none). */
    uint16_t port;        /**< Actual port (set after listen, useful when port=0). */

    /** Receive buffer for incoming data accumulation. */
    char     recv_buf[CLIENT_STATE_RECV_BUF];
    uint32_t recv_len;    /**< Bytes currently in recv_buf. */
} client_state_socket_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize state socket to default (not listening).
 * @param css  Socket to initialize (non-NULL).
 */
void client_state_socket_init(client_state_socket_t *css);

/**
 * @brief Start listening on the given port.
 *
 * Pass port=0 to let the OS assign a free port (stored in css->port).
 * The listener is set to non-blocking and SO_REUSEADDR.
 *
 * @param css   Socket (non-NULL, must be initialized).
 * @param port  TCP port to listen on (0 = OS-assigned).
 * @return true on success.
 */
bool client_state_socket_listen(client_state_socket_t *css, uint16_t port);

/**
 * @brief Close all sockets and reset state.
 * @param css  Socket (non-NULL).
 */
void client_state_socket_destroy(client_state_socket_t *css);

/* ------------------------------------------------------------------------ */
/* I/O (call each frame)                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief Poll for new connections and incoming data (non-blocking).
 *
 * Accepts a pending connection if no controller is connected.
 * Reads available data from the connected controller into recv_buf.
 * Detects controller disconnect and resets client_fd to -1.
 *
 * Call once per frame in the main loop.
 *
 * @param css  Socket (non-NULL, must be listening).
 * @return true if any activity occurred (accept or data received).
 */
bool client_state_socket_poll(client_state_socket_t *css);

/**
 * @brief Pop a complete newline-delimited line from the receive buffer.
 *
 * Copies the line (without trailing newline) into the caller's buffer.
 *
 * @param css      Socket (non-NULL).
 * @param buf      Output buffer for the line.
 * @param buf_cap  Capacity of output buffer.
 * @return Length of the line, or 0 if no complete line available.
 */
uint32_t client_state_socket_pop_line(client_state_socket_t *css,
                                       char *buf, uint32_t buf_cap);

/**
 * @brief Send a JSON message to the connected controller.
 *
 * Appends a newline if not already present. Returns false if no
 * controller is connected or the send fails.
 *
 * @param css   Socket (non-NULL).
 * @param json  JSON string to send.
 * @param len   Length of the JSON string.
 * @return true if sent successfully.
 */
bool client_state_socket_send(client_state_socket_t *css,
                               const char *json, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_CLIENT_STATE_SOCKET_H */

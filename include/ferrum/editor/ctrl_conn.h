/**
 * @file ctrl_conn.h
 * @brief Controller TCP connection to editor server.
 *
 * Manages a non-blocking TCP connection to the edit protocol server.
 * Sends JSON commands, receives newline-delimited JSON responses.
 *
 * Thread safety: single-threaded (controller main loop only).
 */
#ifndef FERRUM_EDITOR_CTRL_CONN_H
#define FERRUM_EDITOR_CTRL_CONN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------------ */
/* Constants                                                                 */
/* ------------------------------------------------------------------------ */

#define CTRL_CONN_RECV_BUF  8192
#define CTRL_CONN_SEND_BUF  8192

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief Connection state.
 */
typedef enum ctrl_conn_state {
    CTRL_CONN_DISCONNECTED = 0,
    CTRL_CONN_CONNECTED    = 1,
    CTRL_CONN_ERROR        = 2,
} ctrl_conn_state_t;

/**
 * @brief Controller-to-server TCP connection.
 *
 * Owns the socket fd and read/write buffers.
 * Responses are accumulated in recv_buf until a full line is available.
 *
 * Ownership: connect() opens; disconnect() closes.
 */
typedef struct ctrl_conn {
    int                fd;              /**< Socket fd (-1 = not connected). */
    ctrl_conn_state_t  state;           /**< Connection state. */

    /* Receive buffer for response accumulation. */
    char     recv_buf[CTRL_CONN_RECV_BUF];
    uint32_t recv_len;                  /**< Bytes in recv_buf. */

    /* Next request ID for correlation. */
    uint32_t next_id;
} ctrl_conn_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize connection state (does not connect).
 * @param conn  Connection to initialize.
 */
void ctrl_conn_init(ctrl_conn_t *conn);

/**
 * @brief Connect to the editor server.
 * @param conn  Connection.
 * @param host  Server IP address (e.g., "127.0.0.1").
 * @param port  Server port.
 * @return true on success.
 */
bool ctrl_conn_connect(ctrl_conn_t *conn, const char *host, uint16_t port);

/**
 * @brief Disconnect from the server.
 * @param conn  Connection.
 */
void ctrl_conn_disconnect(ctrl_conn_t *conn);

/* ------------------------------------------------------------------------ */
/* I/O                                                                       */
/* ------------------------------------------------------------------------ */

/**
 * @brief Send a command string to the server.
 *
 * Wraps the command in JSON protocol format: {"id":N,"cmd":"<text>","args":{}}
 * and appends a newline. For commands with structured args, use send_raw.
 *
 * @param conn  Connection.
 * @param cmd   Command text (e.g., "spawn box 1 2 3").
 * @return true if sent successfully.
 */
bool ctrl_conn_send_cmd(ctrl_conn_t *conn, const char *cmd);

/**
 * @brief Send a raw JSON string (already formatted) to the server.
 *
 * Appends a newline if not already present.
 *
 * @param conn  Connection.
 * @param json  JSON command string.
 * @param len   Length of JSON string.
 * @return true if sent successfully.
 */
bool ctrl_conn_send_raw(ctrl_conn_t *conn, const char *json, uint32_t len);

/**
 * @brief Read available data from the server socket.
 *
 * Call this when poll() indicates POLLIN on conn->fd. Accumulates
 * data in recv_buf. Use ctrl_conn_pop_line() to extract complete lines.
 *
 * @param conn  Connection.
 * @return true if data was read, false on error/disconnect.
 */
bool ctrl_conn_recv(ctrl_conn_t *conn);

/**
 * @brief Pop a complete newline-delimited line from the recv buffer.
 *
 * Copies the line into the caller's buffer (without the trailing newline).
 *
 * @param conn     Connection.
 * @param buf      Output buffer.
 * @param buf_cap  Capacity of output buffer.
 * @return Length of the line, or 0 if no complete line available.
 */
uint32_t ctrl_conn_pop_line(ctrl_conn_t *conn, char *buf, uint32_t buf_cap);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_CTRL_CONN_H */

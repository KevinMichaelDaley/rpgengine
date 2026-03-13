/**
 * @file scene_connection.h
 * @brief Scene editor server connection — TCP commands + UDP snapshots.
 *
 * Aggregates a TCP control channel (via ctrl_conn_t) and a UDP
 * replication channel (via net_udp_socket_t) into a single connection
 * context for the scene editor.
 *
 * Thread safety: single-threaded (scene editor main loop only).
 */
#ifndef FERRUM_EDITOR_SCENE_CONNECTION_H
#define FERRUM_EDITOR_SCENE_CONNECTION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/editor/ctrl_conn.h"
#include "ferrum/net/udp_socket.h"

/* ---- Types ---- */

/**
 * @brief Connection state enum.
 */
typedef enum scene_conn_state {
    SCENE_CONN_DISCONNECTED = 0,
    SCENE_CONN_CONNECTED    = 1,
    SCENE_CONN_ERROR        = 2,
} scene_conn_state_t;

/**
 * @brief Configuration for scene connection.
 */
typedef struct scene_conn_config {
    const char *host;       /**< Server hostname or IP (must not be NULL). */
    uint16_t    tcp_port;   /**< TCP port for edit commands. */
    uint16_t    udp_port;   /**< UDP port for replication snapshots. */
} scene_conn_config_t;

/**
 * @brief Scene editor connection state.
 *
 * Owns a TCP control channel and a UDP replication channel.
 * Ownership: init() allocates, destroy() frees.
 */
typedef struct scene_connection {
    ctrl_conn_t        tcp;           /**< TCP connection for commands. */
    net_udp_socket_t   udp;           /**< UDP socket for snapshots. */
    net_udp_addr_t     server_addr;   /**< Server UDP address. */
    scene_conn_state_t state;         /**< Overall connection state. */
    scene_conn_config_t config;       /**< Stored config (host copied). */
    char               host_buf[256]; /**< Internal copy of hostname. */
    uint32_t           next_cmd_id;   /**< Auto-incrementing request ID. */
    uint32_t           pending_cmds;  /**< Commands awaiting response. */
    char               error_msg[256];/**< Last error description. */
    bool               initialized;   /**< True after successful init. */
} scene_connection_t;

/* ---- Lifecycle ---- */

/**
 * @brief Initialize connection state.
 *
 * Allocates TCP receive buffer and prepares UDP socket.
 * Does NOT connect — call scene_connection_connect() separately.
 *
 * @param conn    Connection to initialize (must not be NULL).
 * @param config  Configuration (must not be NULL, host must not be NULL).
 * @return true on success.
 */
bool scene_connection_init(scene_connection_t *conn,
                           const scene_conn_config_t *config);

/**
 * @brief Destroy connection and free resources.
 *
 * Safe to call on already-destroyed or NULL connections.
 *
 * @param conn  Connection to destroy (may be NULL).
 */
void scene_connection_destroy(scene_connection_t *conn);

/**
 * @brief Connect to server (TCP + UDP).
 *
 * @param conn  Initialized connection.
 * @return true if TCP connection succeeded.
 */
bool scene_connection_connect(scene_connection_t *conn);

/**
 * @brief Disconnect from server.
 *
 * @param conn  Connection to disconnect.
 */
void scene_connection_disconnect(scene_connection_t *conn);

/* ---- I/O ---- */

/**
 * @brief Send a command to the server.
 *
 * Wraps the command in JSON and sends over TCP.
 * Returns false if not connected or send fails.
 *
 * @param conn  Connection.
 * @param cmd   Command string (e.g., "spawn box").
 * @return true if sent successfully.
 *
 * Side effects: increments pending_cmds and next_cmd_id.
 */
bool scene_connection_send_cmd(scene_connection_t *conn, const char *cmd);

/**
 * @brief Poll both TCP and UDP channels for incoming data.
 *
 * Non-blocking. Reads available data from both channels.
 *
 * @param conn  Connection.
 * @return Number of events processed (0 if nothing available).
 */
int scene_connection_pump(scene_connection_t *conn);

/**
 * @brief Extract next complete response line from TCP buffer.
 *
 * @param conn     Connection.
 * @param buf      Output buffer for response line.
 * @param buf_cap  Buffer capacity.
 * @return Length of response, or 0 if no complete response available.
 */
uint32_t scene_connection_pop_response(scene_connection_t *conn,
                                       char *buf, uint32_t buf_cap);

/* ---- Status ---- */

/**
 * @brief Get the next auto-incrementing command ID.
 *
 * @param conn  Connection.
 * @return Next ID (starts at 1).
 */
uint32_t scene_connection_next_id(scene_connection_t *conn);

/**
 * @brief Format connection status for display.
 *
 * Produces strings like "Synced", "Syncing... (3)", "Offline", "Error: ...".
 *
 * @param conn  Connection.
 * @param buf   Output buffer.
 * @param cap   Buffer capacity.
 */
void scene_connection_format_status(const scene_connection_t *conn,
                                    char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_CONNECTION_H */

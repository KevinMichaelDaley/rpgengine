/**
 * @file edit_asset_serve.h
 * @brief Server-side asset download — binary TCP protocol.
 *
 * Runs a dedicated pthread with an epoll event loop. Accepts TCP
 * connections and serves asset files using a simple binary protocol:
 *
 *   Request:  u16 LE path_len | utf8 path
 *   Response: u8 status | u32 LE total_len | raw data
 *
 * Status codes: 0=OK, 1=not found, 2=error.
 *
 * Thread safety:
 * - The server thread owns all sockets.
 * - The asset registry is read-only (safe for concurrent access).
 * - The asset_root path is read-only.
 *
 * Public types: edit_asset_server_t (1).
 */
#ifndef FERRUM_EDITOR_ASSETS_EDIT_ASSET_SERVE_H
#define FERRUM_EDITOR_ASSETS_EDIT_ASSET_SERVE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <stdatomic.h>

/* Forward declarations. */
struct edit_asset_registry;

/* ------------------------------------------------------------------------ */
/* Constants                                                                 */
/* ------------------------------------------------------------------------ */

/** @brief Default asset download port. */
#define EDIT_ASSET_DEFAULT_PORT  9200

/** @brief Maximum simultaneous asset download clients. */
#define EDIT_ASSET_MAX_CLIENTS   4

/** @brief Maximum asset request path length. */
#define EDIT_ASSET_MAX_PATH      1024

/** @brief Status: file served successfully. */
#define EDIT_ASSET_STATUS_OK         0

/** @brief Status: file not found in registry. */
#define EDIT_ASSET_STATUS_NOT_FOUND  1

/** @brief Status: server error (bad request, I/O failure). */
#define EDIT_ASSET_STATUS_ERROR      2

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief Per-client connection state for asset downloads.
 */
typedef struct edit_asset_client {
    int fd;  /**< Socket fd (-1 = unused). */
} edit_asset_client_t;

/**
 * @brief Asset download server context.
 *
 * Owns the listen socket, epoll instance, and client array.
 * Borrows the asset registry and asset root path (not owned).
 */
typedef struct edit_asset_server {
    pthread_t                     thread;      /**< Server thread. */
    atomic_bool                   running;     /**< Shutdown flag. */
    int                           listen_fd;   /**< Listen socket. */
    int                           epoll_fd;    /**< epoll instance. */
    uint16_t                      port;        /**< Actual bound port. */

    edit_asset_client_t           clients[EDIT_ASSET_MAX_CLIENTS];
    const struct edit_asset_registry *registry; /**< Borrowed. */
    const char                   *asset_root;  /**< Borrowed root dir path. */
} edit_asset_server_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Start the asset download server.
 *
 * Binds to the given port and spawns the server thread.
 *
 * @param server     Server context to initialize (non-NULL).
 * @param port       TCP port (0 = ephemeral).
 * @param registry   Asset registry for path lookups (borrowed, non-NULL).
 * @param asset_root Filesystem root for asset files (borrowed, non-NULL).
 * @return true on success.
 *
 * @note Ownership: server does NOT own registry or asset_root.
 * @note Nullability: all params must be non-NULL except port can be 0.
 * @note Side effects: spawns a pthread, binds a TCP socket.
 */
bool edit_asset_server_start(edit_asset_server_t *server, uint16_t port,
                              const struct edit_asset_registry *registry,
                              const char *asset_root);

/**
 * @brief Stop the asset server and close all connections.
 *
 * @param server  Server to stop. NULL-safe.
 */
void edit_asset_server_stop(edit_asset_server_t *server);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_ASSETS_EDIT_ASSET_SERVE_H */

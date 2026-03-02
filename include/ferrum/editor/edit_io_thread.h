/**
 * @file edit_io_thread.h
 * @brief Editor I/O thread — epoll-based TCP gateway for edit commands.
 *
 * Runs a dedicated pthread with an epoll event loop. Accepts edit protocol
 * connections, reads newline-delimited JSON, pushes commands into the
 * SPSC command ring, and sends responses from the response ring back
 * to the appropriate client.
 *
 * Thread safety:
 * - Only the I/O thread touches TCP sockets.
 * - Only the main tick thread calls edit_io_drain_responses().
 * - Communication via lock-free SPSC rings.
 */
#ifndef FERRUM_EDITOR_EDIT_IO_THREAD_H
#define FERRUM_EDITOR_EDIT_IO_THREAD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <stdatomic.h>
#include "ferrum/editor/edit_cmd_ring.h"

/* ------------------------------------------------------------------------ */
/* Configuration                                                             */
/* ------------------------------------------------------------------------ */

/** @brief Default edit protocol listen port. */
#define EDIT_IO_DEFAULT_PORT  9100

/** @brief Maximum simultaneous edit clients. */
#define EDIT_IO_MAX_CLIENTS   8

/** @brief Maximum bytes per JSON line (32 MB). */
#define EDIT_IO_MAX_LINE      (32 * 1024 * 1024)

/** @brief Per-client read buffer size (32 MB — must handle asset transfers). */
#define EDIT_IO_READ_BUF_SIZE (32 * 1024 * 1024)

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief Per-client connection state.
 */
typedef struct edit_io_client {
    int      fd;                        /**< Socket fd (-1 = unused). */
    char    *read_buf;                  /**< Accumulation buffer for partial reads. */
    uint32_t read_len;                  /**< Bytes in read_buf. */
    uint32_t read_cap;                  /**< Capacity of read_buf. */
} edit_io_client_t;

/**
 * @brief Editor I/O thread context.
 *
 * Owns the listen socket, epoll instance, client array, and references
 * to the command/response rings.
 *
 * Ownership:
 * - Created/destroyed by edit_io_start / edit_io_stop.
 * - The cmd_ring and resp_ring pointers are borrowed (not owned).
 */
typedef struct edit_io_thread {
    pthread_t           thread;             /**< Thread handle. */
    atomic_bool         running;            /**< Shutdown flag. */
    int                 listen_fd;          /**< Edit protocol listen socket. */
    int                 epoll_fd;           /**< epoll instance fd. */
    uint16_t            port;               /**< Actual bound port. */

    edit_io_client_t    clients[EDIT_IO_MAX_CLIENTS]; /**< Client array. */
    edit_cmd_ring_t    *cmd_ring;           /**< Commands → tick thread. */
    edit_cmd_ring_t    *resp_ring;          /**< Responses ← tick thread. */
} edit_io_thread_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Start the editor I/O thread.
 *
 * Binds to the given port, creates the epoll instance, and spawns
 * the thread. The thread runs until edit_io_stop() is called.
 *
 * @param io         I/O thread context to initialize.
 * @param port       TCP port to listen on (0 = use default 9100).
 * @param cmd_ring   Ring for pushing commands to tick thread.
 * @param resp_ring  Ring for reading responses from tick thread.
 * @return true on success, false on bind/listen/thread failure.
 */
bool edit_io_start(edit_io_thread_t *io, uint16_t port,
                   edit_cmd_ring_t *cmd_ring, edit_cmd_ring_t *resp_ring);

/**
 * @brief Stop the I/O thread and close all connections.
 *
 * Sets the shutdown flag and joins the thread. Closes all client
 * sockets and the listen socket.
 *
 * @param io  I/O thread context to stop.
 */
void edit_io_stop(edit_io_thread_t *io);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_IO_THREAD_H */

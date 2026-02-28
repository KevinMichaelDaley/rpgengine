/**
 * @file client_asset_download.h
 * @brief Client-side asset downloader — binary TCP protocol.
 *
 * Connects to the server's asset download port and requests files
 * using the binary protocol:
 *   Request:  u16 LE path_len | utf8 path
 *   Response: u8 status | u32 LE total_len | raw data
 *
 * Thread safety: single-threaded (call from download/IO thread only).
 *
 * Public types: asset_download_t, asset_download_result_t (2).
 */
#ifndef FERRUM_EDITOR_CLIENT_ASSET_DOWNLOAD_H
#define FERRUM_EDITOR_CLIENT_ASSET_DOWNLOAD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------------ */
/* Constants                                                                 */
/* ------------------------------------------------------------------------ */

/** @brief Download status: success. */
#define ASSET_DL_OK         0

/** @brief Download status: file not found on server. */
#define ASSET_DL_NOT_FOUND  1

/** @brief Download status: server error. */
#define ASSET_DL_ERROR      2

/** @brief Download status: connection/network failure. */
#define ASSET_DL_NET_ERROR  3

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief Asset download connection.
 *
 * Maintains a persistent TCP connection to the server for
 * sequential asset requests.
 */
typedef struct asset_download {
    int      fd;     /**< TCP socket fd (-1 = disconnected). */
    uint16_t port;   /**< Server port. */
    uint32_t host;   /**< Server IPv4 address (network order). */
} asset_download_t;

/**
 * @brief Result of an asset download request.
 *
 * Caller owns the data buffer and must free() it.
 */
typedef struct asset_download_result {
    uint8_t  status;  /**< Status code (ASSET_DL_*). */
    void    *data;    /**< File data (caller-owned, NULL on error). */
    uint32_t size;    /**< Size of data in bytes. */
} asset_download_result_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize the downloader (disconnected state).
 * @param dl  Downloader context (non-NULL).
 */
void asset_download_init(asset_download_t *dl);

/**
 * @brief Connect to the asset download server.
 *
 * @param dl    Downloader context.
 * @param host  Server IPv4 address string (e.g., "127.0.0.1").
 * @param port  Server asset download port.
 * @return true on success.
 *
 * @note Side effects: opens a TCP socket.
 */
bool asset_download_connect(asset_download_t *dl,
                             const char *host, uint16_t port);

/**
 * @brief Disconnect from the server.
 * @param dl  Downloader context. NULL-safe.
 */
void asset_download_disconnect(asset_download_t *dl);

/* ------------------------------------------------------------------------ */
/* Requests                                                                  */
/* ------------------------------------------------------------------------ */

/**
 * @brief Request a single asset by path.
 *
 * Sends the binary request and blocks until the response is received.
 * On success, result.data is heap-allocated and must be freed by caller.
 *
 * @param dl    Downloader (must be connected).
 * @param path  Asset path relative to project root.
 * @param out   Result output (non-NULL).
 * @return true if the protocol exchange completed (check out->status).
 *         false on network failure (connection is closed).
 *
 * @note Ownership: out->data is caller-owned.
 * @note Side effects: blocking network I/O.
 */
bool asset_download_request(asset_download_t *dl, const char *path,
                             asset_download_result_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_CLIENT_ASSET_DOWNLOAD_H */

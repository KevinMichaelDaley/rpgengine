/**
 * @file client_asset_download.c
 * @brief Client-side asset downloader — TCP binary protocol.
 *
 * Non-static functions: init, connect, disconnect, request (4).
 */

#include "ferrum/editor/client/client_asset_download.h"
#include "ferrum/editor/assets/edit_asset_serve.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

/* ----------------------------------------------------------------------- */
/* Internal helpers                                                          */
/* ----------------------------------------------------------------------- */

/** @brief Read exactly n bytes, blocking. */
static bool read_exact_(int fd, void *buf, size_t n) {
    size_t done = 0;
    while (done < n) {
        ssize_t r = recv(fd, (char *)buf + done, n - done, 0);
        if (r <= 0) return false;
        done += (size_t)r;
    }
    return true;
}

/** @brief Send exactly n bytes. */
static bool send_exact_(int fd, const void *buf, size_t n) {
    size_t done = 0;
    while (done < n) {
        ssize_t w = send(fd, (const char *)buf + done, n - done, MSG_NOSIGNAL);
        if (w <= 0) return false;
        done += (size_t)w;
    }
    return true;
}

/* ----------------------------------------------------------------------- */
/* Lifecycle                                                                 */
/* ----------------------------------------------------------------------- */

void asset_download_init(asset_download_t *dl) {
    if (!dl) return;
    memset(dl, 0, sizeof(*dl));
    dl->fd = -1;
}

bool asset_download_connect(asset_download_t *dl,
                             const char *host, uint16_t port) {
    if (!dl || !host) return false;
    if (dl->fd >= 0) {
        close(dl->fd);
        dl->fd = -1;
    }

    dl->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (dl->fd < 0) return false;

    int one = 1;
    setsockopt(dl->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        close(dl->fd);
        dl->fd = -1;
        return false;
    }

    if (connect(dl->fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(dl->fd);
        dl->fd = -1;
        return false;
    }

    dl->port = port;
    dl->host = addr.sin_addr.s_addr;
    return true;
}

void asset_download_disconnect(asset_download_t *dl) {
    if (!dl) return;
    if (dl->fd >= 0) {
        close(dl->fd);
        dl->fd = -1;
    }
}

/* ----------------------------------------------------------------------- */
/* Request                                                                   */
/* ----------------------------------------------------------------------- */

bool asset_download_request(asset_download_t *dl, const char *path,
                             asset_download_result_t *out) {
    if (!dl || !path || !out) return false;
    memset(out, 0, sizeof(*out));

    if (dl->fd < 0) {
        out->status = ASSET_DL_NET_ERROR;
        return false;
    }

    size_t path_len = strlen(path);
    if (path_len > EDIT_ASSET_MAX_PATH) {
        out->status = ASSET_DL_ERROR;
        return false;
    }

    /* Send request: u16 LE path_len + path. */
    uint8_t hdr[2];
    hdr[0] = (uint8_t)(path_len & 0xFF);
    hdr[1] = (uint8_t)((path_len >> 8) & 0xFF);
    if (!send_exact_(dl->fd, hdr, 2) ||
        (path_len > 0 && !send_exact_(dl->fd, path, path_len))) {
        asset_download_disconnect(dl);
        out->status = ASSET_DL_NET_ERROR;
        return false;
    }

    /* Read response: u8 status + u32 LE total_len. */
    uint8_t resp_hdr[5];
    if (!read_exact_(dl->fd, resp_hdr, 5)) {
        asset_download_disconnect(dl);
        out->status = ASSET_DL_NET_ERROR;
        return false;
    }

    out->status = resp_hdr[0];
    out->size = (uint32_t)resp_hdr[1]
              | ((uint32_t)resp_hdr[2] << 8)
              | ((uint32_t)resp_hdr[3] << 16)
              | ((uint32_t)resp_hdr[4] << 24);

    /* Read data if any. */
    if (out->size > 0) {
        out->data = malloc(out->size);
        if (!out->data) {
            asset_download_disconnect(dl);
            out->status = ASSET_DL_NET_ERROR;
            return false;
        }
        if (!read_exact_(dl->fd, out->data, out->size)) {
            free(out->data);
            out->data = NULL;
            asset_download_disconnect(dl);
            out->status = ASSET_DL_NET_ERROR;
            return false;
        }
    }

    return true;
}

/**
 * @file edit_asset_serve_tests.c
 * @brief Tests for server-side asset TCP serving (binary protocol).
 *
 * Wire format:
 *   Request:  u16 LE path_len | utf8 path
 *   Response: u8 status | u32 LE total_len | raw data
 *   Status codes: 0=OK, 1=not found, 2=error
 */

#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "ferrum/editor/edit_asset_registry.h"
#include "ferrum/editor/assets/edit_asset_serve.h"

/* ----------------------------------------------------------------------- */
/* Test macros                                                               */
/* ----------------------------------------------------------------------- */

static int g_pass, g_fail;

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); \
    if (fn()) { printf("OK   %s\n", #fn); g_pass++; } \
    else       { printf("FAIL %s\n", #fn); g_fail++; } \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  ASSERT FAILED: %s (line %d)\n", #cond, __LINE__); \
        return false; \
    } \
} while (0)

/* ----------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ----------------------------------------------------------------------- */

/** @brief Create a temp directory for test assets. */
static char g_tmpdir[256];

static void make_tmpdir_(void) {
    snprintf(g_tmpdir, sizeof(g_tmpdir), "/tmp/asset_serve_test_XXXXXX");
    char *r = mkdtemp(g_tmpdir);
    (void)r;
}

/** @brief Write a file with given content. */
static void write_file_(const char *dir, const char *rel_path,
                         const char *data, size_t len) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, rel_path);

    /* Create parent directories. */
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + strlen(dir) + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }

    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(data, 1, len, f);
        fclose(f);
    }
}

/** @brief Remove temp directory recursively. */
static void rm_tmpdir_(void) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_tmpdir);
    int r = system(cmd);
    (void)r;
}

/** @brief Connect to a TCP port on loopback. Returns fd or -1. */
static int connect_to_(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/** @brief Send a binary asset request: u16 LE path_len + path. */
static bool send_request_(int fd, const char *path) {
    uint16_t path_len = (uint16_t)strlen(path);
    uint8_t header[2];
    header[0] = (uint8_t)(path_len & 0xFF);
    header[1] = (uint8_t)((path_len >> 8) & 0xFF);
    if (send(fd, header, 2, 0) != 2) return false;
    if (path_len > 0) {
        if (send(fd, path, path_len, 0) != (ssize_t)path_len) return false;
    }
    return true;
}

/** @brief Read exactly n bytes from fd, blocking. */
static bool read_exact_(int fd, void *buf, size_t n) {
    size_t total = 0;
    while (total < n) {
        ssize_t r = recv(fd, (char *)buf + total, n - total, 0);
        if (r <= 0) return false;
        total += (size_t)r;
    }
    return true;
}

/** @brief Read a binary asset response: u8 status + u32 LE total_len + data. */
static bool recv_response_(int fd, uint8_t *status_out,
                            void **data_out, uint32_t *len_out) {
    /* Read status byte. */
    if (!read_exact_(fd, status_out, 1)) return false;

    /* Read total_len (u32 LE). */
    uint8_t len_buf[4];
    if (!read_exact_(fd, len_buf, 4)) return false;
    *len_out = (uint32_t)len_buf[0]
             | ((uint32_t)len_buf[1] << 8)
             | ((uint32_t)len_buf[2] << 16)
             | ((uint32_t)len_buf[3] << 24);

    /* Read data. */
    if (*len_out > 0) {
        *data_out = malloc(*len_out);
        if (!*data_out) return false;
        if (!read_exact_(fd, *data_out, *len_out)) {
            free(*data_out);
            *data_out = NULL;
            return false;
        }
    } else {
        *data_out = NULL;
    }
    return true;
}

/* ----------------------------------------------------------------------- */
/* Test context                                                              */
/* ----------------------------------------------------------------------- */

typedef struct test_ctx {
    edit_asset_registry_t  reg;
    edit_asset_server_t    server;
} test_ctx_t;

static bool setup_(test_ctx_t *t) {
    make_tmpdir_();

    /* Create test assets. */
    write_file_(g_tmpdir, "meshes/box.glb", "GLBDATA_BOX", 11);
    write_file_(g_tmpdir, "textures/brick.png", "PNGDATA_BRICK_1234", 18);

    /* Scan into registry. */
    edit_asset_registry_init(&t->reg, 64);
    uint32_t n = edit_asset_registry_scan(&t->reg, g_tmpdir);
    if (n < 2) return false;

    /* Start asset server on ephemeral port. */
    if (!edit_asset_server_start(&t->server, 0, &t->reg, g_tmpdir)) {
        return false;
    }
    /* Give the thread a moment to start listening. */
    usleep(50000);
    return true;
}

static void teardown_(test_ctx_t *t) {
    edit_asset_server_stop(&t->server);
    edit_asset_registry_destroy(&t->reg);
    rm_tmpdir_();
}

/* ----------------------------------------------------------------------- */
/* Tests                                                                     */
/* ----------------------------------------------------------------------- */

/** Download a file that exists. */
static bool test_download_ok(void) {
    test_ctx_t t;
    ASSERT(setup_(&t));

    int fd = connect_to_(t.server.port);
    ASSERT(fd >= 0);

    ASSERT(send_request_(fd, "meshes/box.glb"));

    uint8_t status;
    void *data = NULL;
    uint32_t len = 0;
    ASSERT(recv_response_(fd, &status, &data, &len));
    ASSERT(status == 0);  /* OK */
    ASSERT(len == 11);
    ASSERT(memcmp(data, "GLBDATA_BOX", 11) == 0);

    free(data);
    close(fd);
    teardown_(&t);
    return true;
}

/** Request a file that doesn't exist → status 1 (not found). */
static bool test_download_not_found(void) {
    test_ctx_t t;
    ASSERT(setup_(&t));

    int fd = connect_to_(t.server.port);
    ASSERT(fd >= 0);

    ASSERT(send_request_(fd, "meshes/nonexistent.glb"));

    uint8_t status;
    void *data = NULL;
    uint32_t len = 0;
    ASSERT(recv_response_(fd, &status, &data, &len));
    ASSERT(status == 1);  /* Not found */
    ASSERT(len == 0);

    free(data);
    close(fd);
    teardown_(&t);
    return true;
}

/** Multiple sequential requests on same connection. */
static bool test_sequential_requests(void) {
    test_ctx_t t;
    ASSERT(setup_(&t));

    int fd = connect_to_(t.server.port);
    ASSERT(fd >= 0);

    /* First request: box.glb */
    ASSERT(send_request_(fd, "meshes/box.glb"));
    uint8_t status;
    void *data = NULL;
    uint32_t len = 0;
    ASSERT(recv_response_(fd, &status, &data, &len));
    ASSERT(status == 0);
    ASSERT(len == 11);
    free(data);

    /* Second request: brick.png */
    ASSERT(send_request_(fd, "textures/brick.png"));
    data = NULL;
    ASSERT(recv_response_(fd, &status, &data, &len));
    ASSERT(status == 0);
    ASSERT(len == 18);
    ASSERT(memcmp(data, "PNGDATA_BRICK_1234", 18) == 0);
    free(data);

    /* Third: not found */
    ASSERT(send_request_(fd, "nope.xyz"));
    data = NULL;
    ASSERT(recv_response_(fd, &status, &data, &len));
    ASSERT(status == 1);

    free(data);
    close(fd);
    teardown_(&t);
    return true;
}

/** Empty path request → status 2 (error). */
static bool test_empty_path(void) {
    test_ctx_t t;
    ASSERT(setup_(&t));

    int fd = connect_to_(t.server.port);
    ASSERT(fd >= 0);

    ASSERT(send_request_(fd, ""));

    uint8_t status;
    void *data = NULL;
    uint32_t len = 0;
    ASSERT(recv_response_(fd, &status, &data, &len));
    ASSERT(status == 2);  /* Error */
    ASSERT(len == 0);

    free(data);
    close(fd);
    teardown_(&t);
    return true;
}

/** Server handles client disconnect gracefully. */
static bool test_client_disconnect(void) {
    test_ctx_t t;
    ASSERT(setup_(&t));

    int fd = connect_to_(t.server.port);
    ASSERT(fd >= 0);
    close(fd);
    usleep(50000);

    /* Server should still be running for new connections. */
    fd = connect_to_(t.server.port);
    ASSERT(fd >= 0);
    ASSERT(send_request_(fd, "meshes/box.glb"));

    uint8_t status;
    void *data = NULL;
    uint32_t len = 0;
    ASSERT(recv_response_(fd, &status, &data, &len));
    ASSERT(status == 0);

    free(data);
    close(fd);
    teardown_(&t);
    return true;
}

/** Null-safety: start/stop with NULL doesn't crash. */
static bool test_null_safety(void) {
    edit_asset_server_stop(NULL);
    ASSERT(!edit_asset_server_start(NULL, 0, NULL, NULL));
    return true;
}

/** Multiple concurrent clients. */
static bool test_concurrent_clients(void) {
    test_ctx_t t;
    ASSERT(setup_(&t));

    int fd1 = connect_to_(t.server.port);
    int fd2 = connect_to_(t.server.port);
    ASSERT(fd1 >= 0);
    ASSERT(fd2 >= 0);

    /* Both send requests. */
    ASSERT(send_request_(fd1, "meshes/box.glb"));
    ASSERT(send_request_(fd2, "textures/brick.png"));

    uint8_t s1, s2;
    void *d1 = NULL, *d2 = NULL;
    uint32_t l1 = 0, l2 = 0;
    ASSERT(recv_response_(fd1, &s1, &d1, &l1));
    ASSERT(recv_response_(fd2, &s2, &d2, &l2));
    ASSERT(s1 == 0 && l1 == 11);
    ASSERT(s2 == 0 && l2 == 18);

    free(d1);
    free(d2);
    close(fd1);
    close(fd2);
    teardown_(&t);
    return true;
}

int main(void) {
    RUN(test_download_ok);
    RUN(test_download_not_found);
    RUN(test_sequential_requests);
    RUN(test_empty_path);
    RUN(test_client_disconnect);
    RUN(test_null_safety);
    RUN(test_concurrent_clients);

    printf("\n%d / %d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}

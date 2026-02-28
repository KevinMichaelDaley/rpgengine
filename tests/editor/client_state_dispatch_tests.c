/**
 * @file client_state_dispatch_tests.c
 * @brief Tests for client state socket dispatch (query/command routing).
 *
 * Tests use a loopback connection to send JSON messages and verify
 * the dispatch routes them correctly to cursor state handlers.
 */

#define _DEFAULT_SOURCE

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>

#include "ferrum/editor/client/client_state_socket.h"
#include "ferrum/editor/client/client_state_dispatch.h"
#include "ferrum/editor/client/client_cursor.h"
#include "ferrum/editor/json_parse.h"

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

#define ASSERT_NEAR(a, b, eps) \
    ASSERT(fabs((double)(a) - (double)(b)) < (eps))

/* ----------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ----------------------------------------------------------------------- */

static int connect_to_(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    return fd;
}

static bool raw_send_(int fd, const char *msg) {
    size_t len = strlen(msg);
    return send(fd, msg, len, MSG_NOSIGNAL) == (ssize_t)len;
}

static ssize_t raw_recv_(int fd, char *buf, size_t cap) {
    /* Retry up to ~100ms for data to arrive (non-blocking socket). */
    for (int i = 0; i < 20; i++) {
        ssize_t n = recv(fd, buf, cap, 0);
        if (n > 0) return n;
        usleep(5000);
    }
    return -1;
}

/** Poll with retries (up to ~100ms). */
static bool poll_retry_(client_state_socket_t *css) {
    for (int i = 0; i < 20; i++) {
        if (client_state_socket_poll(css)) return true;
        usleep(5000);
    }
    return false;
}

/* ----------------------------------------------------------------------- */
/* Test context                                                              */
/* ----------------------------------------------------------------------- */

typedef struct test_ctx {
    client_state_socket_t   css;
    editor_cursor_t         cursor;
    client_state_dispatch_t disp;
    int                     ctrl_fd;
} test_ctx_t;

static bool setup_(test_ctx_t *t) {
    client_state_socket_init(&t->css);
    editor_cursor_init(&t->cursor);
    if (!client_state_socket_listen(&t->css, 0)) return false;

    t->ctrl_fd = connect_to_(t->css.port);
    if (t->ctrl_fd < 0) return false;

    if (!poll_retry_(&t->css)) return false;

    client_state_dispatch_init(&t->disp, &t->css, &t->cursor);
    return true;
}

static void teardown_(test_ctx_t *t) {
    close(t->ctrl_fd);
    client_state_socket_destroy(&t->css);
}

/* ----------------------------------------------------------------------- */
/* Tests: Queries                                                            */
/* ----------------------------------------------------------------------- */

/** Query cursor returns current position. */
static bool test_query_cursor(void) {
    test_ctx_t t;
    ASSERT(setup_(&t));

    /* Set cursor to a known position. */
    editor_cursor_set_position(&t.cursor, (vec3_t){5.0f, 10.0f, 15.0f});

    /* Send query. */
    ASSERT(raw_send_(t.ctrl_fd, "{\"query\":\"cursor\"}\n"));
    usleep(50000);
    ASSERT(poll_retry_(&t.css));
    uint32_t processed = client_state_dispatch_drain(&t.disp);
    ASSERT(processed == 1);

    /* Read response. */
    usleep(50000);
    char buf[1024];
    ssize_t n = raw_recv_(t.ctrl_fd, buf, sizeof(buf) - 1);
    ASSERT(n > 0);
    buf[n] = '\0';
    ASSERT(strstr(buf, "\"cursor\"") != NULL);
    ASSERT(strstr(buf, "5") != NULL);
    ASSERT(strstr(buf, "10") != NULL);
    ASSERT(strstr(buf, "15") != NULL);

    teardown_(&t);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Tests: Commands                                                           */
/* ----------------------------------------------------------------------- */

/** set_cursor updates cursor position. */
static bool test_cmd_set_cursor(void) {
    test_ctx_t t;
    ASSERT(setup_(&t));

    ASSERT(raw_send_(t.ctrl_fd,
        "{\"cmd\":\"set_cursor\",\"pos\":[3,6,9]}\n"));
    usleep(50000);
    ASSERT(poll_retry_(&t.css));
    uint32_t processed = client_state_dispatch_drain(&t.disp);
    ASSERT(processed == 1);

    /* Cursor should have snapped (grid=1.0, snap=true). */
    ASSERT_NEAR(t.cursor.position.x, 3.0f, 0.01f);
    ASSERT_NEAR(t.cursor.position.y, 6.0f, 0.01f);
    ASSERT_NEAR(t.cursor.position.z, 9.0f, 0.01f);

    teardown_(&t);
    return true;
}

/** move_cursor moves cursor by delta. */
static bool test_cmd_move_cursor(void) {
    test_ctx_t t;
    ASSERT(setup_(&t));

    /* Start at origin. */
    ASSERT_NEAR(t.cursor.position.x, 0.0f, 0.01f);

    ASSERT(raw_send_(t.ctrl_fd,
        "{\"cmd\":\"move_cursor\",\"delta\":[2,0,-1]}\n"));
    usleep(50000);
    ASSERT(poll_retry_(&t.css));
    client_state_dispatch_drain(&t.disp);

    ASSERT_NEAR(t.cursor.position.x, 2.0f, 0.01f);
    ASSERT_NEAR(t.cursor.position.y, 0.0f, 0.01f);
    ASSERT_NEAR(t.cursor.position.z, -1.0f, 0.01f);

    teardown_(&t);
    return true;
}

/** cursor_visible toggles visibility. */
static bool test_cmd_cursor_visible(void) {
    test_ctx_t t;
    ASSERT(setup_(&t));
    ASSERT(t.cursor.visible == true);

    ASSERT(raw_send_(t.ctrl_fd, "{\"cmd\":\"cursor_visible\"}\n"));
    usleep(50000);
    ASSERT(poll_retry_(&t.css));
    client_state_dispatch_drain(&t.disp);

    ASSERT(t.cursor.visible == false);

    /* Toggle again. */
    ASSERT(raw_send_(t.ctrl_fd, "{\"cmd\":\"cursor_visible\"}\n"));
    usleep(50000);
    ASSERT(poll_retry_(&t.css));
    client_state_dispatch_drain(&t.disp);

    ASSERT(t.cursor.visible == true);

    teardown_(&t);
    return true;
}

/** Multiple commands in one burst. */
static bool test_cmd_burst(void) {
    test_ctx_t t;
    ASSERT(setup_(&t));

    ASSERT(raw_send_(t.ctrl_fd,
        "{\"cmd\":\"set_cursor\",\"pos\":[1,1,1]}\n"
        "{\"cmd\":\"move_cursor\",\"delta\":[2,3,4]}\n"));
    usleep(50000);
    ASSERT(poll_retry_(&t.css));
    uint32_t processed = client_state_dispatch_drain(&t.disp);
    ASSERT(processed == 2);

    ASSERT_NEAR(t.cursor.position.x, 3.0f, 0.01f);
    ASSERT_NEAR(t.cursor.position.y, 4.0f, 0.01f);
    ASSERT_NEAR(t.cursor.position.z, 5.0f, 0.01f);

    teardown_(&t);
    return true;
}

/** Invalid JSON is silently skipped. */
static bool test_invalid_json(void) {
    test_ctx_t t;
    ASSERT(setup_(&t));

    ASSERT(raw_send_(t.ctrl_fd, "not json at all\n"));
    usleep(50000);
    ASSERT(poll_retry_(&t.css));
    /* Should not crash — 1 line consumed, 0 dispatched successfully. */
    uint32_t processed = client_state_dispatch_drain(&t.disp);
    ASSERT(processed == 1);  /* Line was consumed even if parse failed. */

    /* Cursor unchanged. */
    ASSERT_NEAR(t.cursor.position.x, 0.0f, 0.01f);

    teardown_(&t);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Tests: Push events                                                        */
/* ----------------------------------------------------------------------- */

/** Push cursor_moved event reaches controller. */
static bool test_push_cursor_moved(void) {
    test_ctx_t t;
    ASSERT(setup_(&t));

    client_state_push_cursor_moved(&t.disp, 1.5f, 2.5f, 3.5f);
    usleep(50000);

    char buf[1024];
    ssize_t n = raw_recv_(t.ctrl_fd, buf, sizeof(buf) - 1);
    ASSERT(n > 0);
    buf[n] = '\0';
    ASSERT(strstr(buf, "cursor_moved") != NULL);
    ASSERT(strstr(buf, "1.5") != NULL);

    teardown_(&t);
    return true;
}

/** Push entity_clicked event reaches controller. */
static bool test_push_entity_clicked(void) {
    test_ctx_t t;
    ASSERT(setup_(&t));

    client_state_push_entity_clicked(&t.disp, 42, 10.0f, 20.0f, 30.0f);
    usleep(50000);

    char buf[1024];
    ssize_t n = raw_recv_(t.ctrl_fd, buf, sizeof(buf) - 1);
    ASSERT(n > 0);
    buf[n] = '\0';
    ASSERT(strstr(buf, "entity_clicked") != NULL);
    ASSERT(strstr(buf, "42") != NULL);

    teardown_(&t);
    return true;
}

/** Null dispatch doesn't crash. */
static bool test_null_dispatch(void) {
    client_state_dispatch_init(NULL, NULL, NULL);
    ASSERT(client_state_dispatch_drain(NULL) == 0);
    client_state_push_cursor_moved(NULL, 0, 0, 0);
    client_state_push_entity_clicked(NULL, 0, 0, 0, 0);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Main                                                                      */
/* ----------------------------------------------------------------------- */

int main(void) {
    RUN(test_query_cursor);
    RUN(test_cmd_set_cursor);
    RUN(test_cmd_move_cursor);
    RUN(test_cmd_cursor_visible);
    RUN(test_cmd_burst);
    RUN(test_invalid_json);
    RUN(test_push_cursor_moved);
    RUN(test_push_entity_clicked);
    RUN(test_null_dispatch);

    printf("\n%d / %d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}

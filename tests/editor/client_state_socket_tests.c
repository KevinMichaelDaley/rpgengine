/**
 * @file client_state_socket_tests.c
 * @brief Tests for the client-side state socket (TCP listener).
 *
 * The client state socket listens for controller connections and provides
 * bidirectional JSON communication (queries, commands, push events).
 *
 * Tests use loopback connections to exercise the full lifecycle.
 */

#define _DEFAULT_SOURCE  /* For usleep(). */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>

#include "ferrum/editor/client/client_state_socket.h"

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

/** Connect a plain TCP client to the state socket. Returns fd or -1. */
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

    /* Set non-blocking. */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    return fd;
}

/** Send a string on a raw fd. */
static bool raw_send_(int fd, const char *msg) {
    size_t len = strlen(msg);
    return send(fd, msg, len, MSG_NOSIGNAL) == (ssize_t)len;
}

/** Recv into buffer from a raw fd (non-blocking, returns bytes read). */
static ssize_t raw_recv_(int fd, char *buf, size_t cap) {
    return recv(fd, buf, cap, 0);
}

/** Poll the state socket with retries (up to ~100ms). */
static bool poll_retry_(client_state_socket_t *css) {
    for (int i = 0; i < 20; i++) {
        if (client_state_socket_poll(css)) return true;
        usleep(5000);
    }
    return false;
}

/* ----------------------------------------------------------------------- */
/* Tests: Lifecycle                                                          */
/* ----------------------------------------------------------------------- */

/** Init and destroy without connections. */
static bool test_init_destroy(void) {
    client_state_socket_t css;
    client_state_socket_init(&css);
    ASSERT(css.listen_fd == -1);
    ASSERT(css.client_fd == -1);
    ASSERT(css.recv_len == 0);
    client_state_socket_destroy(&css);
    return true;
}

/** Listen on port 0 (OS-assigned) succeeds. */
static bool test_listen(void) {
    client_state_socket_t css;
    client_state_socket_init(&css);
    ASSERT(client_state_socket_listen(&css, 0));
    ASSERT(css.listen_fd >= 0);
    ASSERT(css.port > 0);
    client_state_socket_destroy(&css);
    return true;
}

/** Accept a controller connection. */
static bool test_accept(void) {
    client_state_socket_t css;
    client_state_socket_init(&css);
    ASSERT(client_state_socket_listen(&css, 0));

    int ctrl_fd = connect_to_(css.port);
    ASSERT(ctrl_fd >= 0);

    /* Poll should accept. */
    ASSERT(poll_retry_(&css));
    ASSERT(css.client_fd >= 0);

    close(ctrl_fd);
    client_state_socket_destroy(&css);
    return true;
}

/** Receive a complete line from controller. */
static bool test_recv_line(void) {
    client_state_socket_t css;
    client_state_socket_init(&css);
    ASSERT(client_state_socket_listen(&css, 0));

    int ctrl_fd = connect_to_(css.port);
    ASSERT(ctrl_fd >= 0);
    ASSERT(poll_retry_(&css));

    /* Send a query from controller. */
    ASSERT(raw_send_(ctrl_fd, "{\"query\":\"cursor\"}\n"));
    usleep(50000);

    /* Poll to read data. */
    ASSERT(poll_retry_(&css));

    /* Pop the line. */
    char line[1024];
    uint32_t len = client_state_socket_pop_line(&css, line, sizeof(line));
    ASSERT(len > 0);
    ASSERT(strstr(line, "cursor") != NULL);

    close(ctrl_fd);
    client_state_socket_destroy(&css);
    return true;
}

/** Pop line returns 0 when no complete line is buffered. */
static bool test_pop_line_empty(void) {
    client_state_socket_t css;
    client_state_socket_init(&css);
    char buf[256];
    ASSERT(client_state_socket_pop_line(&css, buf, sizeof(buf)) == 0);
    client_state_socket_destroy(&css);
    return true;
}

/** Send a push event from client to controller. */
static bool test_send_event(void) {
    client_state_socket_t css;
    client_state_socket_init(&css);
    ASSERT(client_state_socket_listen(&css, 0));

    int ctrl_fd = connect_to_(css.port);
    ASSERT(ctrl_fd >= 0);
    ASSERT(poll_retry_(&css));

    /* Push an event. */
    const char *event = "{\"event\":\"cursor_moved\",\"pos\":[1,2,3]}";
    ASSERT(client_state_socket_send(&css, event, (uint32_t)strlen(event)));

    /* Read on the controller side. */
    usleep(50000);
    char buf[1024];
    ssize_t n = raw_recv_(ctrl_fd, buf, sizeof(buf) - 1);
    ASSERT(n > 0);
    buf[n] = '\0';
    ASSERT(strstr(buf, "cursor_moved") != NULL);
    ASSERT(buf[n - 1] == '\n');  /* Newline-delimited. */

    close(ctrl_fd);
    client_state_socket_destroy(&css);
    return true;
}

/** Multiple lines accumulated and popped individually. */
static bool test_multiple_lines(void) {
    client_state_socket_t css;
    client_state_socket_init(&css);
    ASSERT(client_state_socket_listen(&css, 0));

    int ctrl_fd = connect_to_(css.port);
    ASSERT(ctrl_fd >= 0);
    ASSERT(poll_retry_(&css));

    /* Send 3 messages in one burst. */
    ASSERT(raw_send_(ctrl_fd,
        "{\"query\":\"cursor\"}\n"
        "{\"cmd\":\"set_cursor\",\"pos\":[1,2,3]}\n"
        "{\"query\":\"camera\"}\n"));
    usleep(50000);
    ASSERT(poll_retry_(&css));

    char line[1024];
    int count = 0;
    while (client_state_socket_pop_line(&css, line, sizeof(line)) > 0) {
        count++;
    }
    ASSERT(count == 3);

    close(ctrl_fd);
    client_state_socket_destroy(&css);
    return true;
}

/** Controller disconnect is handled gracefully. */
static bool test_disconnect(void) {
    client_state_socket_t css;
    client_state_socket_init(&css);
    ASSERT(client_state_socket_listen(&css, 0));

    int ctrl_fd = connect_to_(css.port);
    ASSERT(ctrl_fd >= 0);
    ASSERT(poll_retry_(&css));
    ASSERT(css.client_fd >= 0);

    /* Disconnect the controller. */
    close(ctrl_fd);
    usleep(50000);

    /* Poll should detect disconnect and reset client_fd. */
    client_state_socket_poll(&css);
    ASSERT(css.client_fd == -1);

    client_state_socket_destroy(&css);
    return true;
}

/** Second connection replaces the first. */
static bool test_reconnect(void) {
    client_state_socket_t css;
    client_state_socket_init(&css);
    ASSERT(client_state_socket_listen(&css, 0));

    /* First connection. */
    int fd1 = connect_to_(css.port);
    ASSERT(fd1 >= 0);
    ASSERT(poll_retry_(&css));
    int first_client = css.client_fd;
    ASSERT(first_client >= 0);

    /* Disconnect first. */
    close(fd1);
    usleep(50000);
    client_state_socket_poll(&css);

    /* Second connection. */
    int fd2 = connect_to_(css.port);
    ASSERT(fd2 >= 0);
    ASSERT(poll_retry_(&css));
    ASSERT(css.client_fd >= 0);

    /* Send/recv on new connection. */
    ASSERT(raw_send_(fd2, "{\"query\":\"cursor\"}\n"));
    usleep(50000);
    ASSERT(poll_retry_(&css));
    char line[1024];
    ASSERT(client_state_socket_pop_line(&css, line, sizeof(line)) > 0);

    close(fd2);
    client_state_socket_destroy(&css);
    return true;
}

/** Send with no client connected returns false. */
static bool test_send_no_client(void) {
    client_state_socket_t css;
    client_state_socket_init(&css);
    ASSERT(!client_state_socket_send(&css, "hello", 5));
    client_state_socket_destroy(&css);
    return true;
}

/** Null params don't crash. */
static bool test_null_params(void) {
    client_state_socket_init(NULL);
    client_state_socket_destroy(NULL);
    ASSERT(!client_state_socket_listen(NULL, 0));
    ASSERT(!client_state_socket_poll(NULL));
    ASSERT(!client_state_socket_send(NULL, NULL, 0));
    ASSERT(client_state_socket_pop_line(NULL, NULL, 0) == 0);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Main                                                                      */
/* ----------------------------------------------------------------------- */

int main(void) {
    RUN(test_init_destroy);
    RUN(test_listen);
    RUN(test_accept);
    RUN(test_recv_line);
    RUN(test_pop_line_empty);
    RUN(test_send_event);
    RUN(test_multiple_lines);
    RUN(test_disconnect);
    RUN(test_reconnect);
    RUN(test_send_no_client);
    RUN(test_null_params);

    printf("\n%d / %d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}

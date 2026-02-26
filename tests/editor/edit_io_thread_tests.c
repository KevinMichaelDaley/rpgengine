/**
 * @file edit_io_thread_tests.c
 * @brief Unit tests for the editor I/O thread.
 *
 * Tests the epoll-based TCP server: start/stop, client connect,
 * send JSON command, receive response, multiple clients.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "ferrum/editor/edit_io_thread.h"
#include "ferrum/editor/edit_cmd_ring.h"

/* ----------------------------------------------------------------------- */
/* Test harness                                                             */
/* ----------------------------------------------------------------------- */

#define ASSERT_TRUE(expr)                                                    \
    do {                                                                     \
        if (!(expr)) {                                                       \
            fprintf(stderr, "  ASSERT_TRUE failed: %s (%s:%d)\n",            \
                    #expr, __FILE__, __LINE__);                               \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_UINT_EQ(a, b)                                                 \
    do {                                                                     \
        unsigned _a = (unsigned)(a), _b = (unsigned)(b);                     \
        if (_a != _b) {                                                      \
            fprintf(stderr, "  ASSERT_UINT_EQ failed: %u != %u (%s:%d)\n",   \
                    _a, _b, __FILE__, __LINE__);                              \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* ----------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ----------------------------------------------------------------------- */

/** @brief Connect to localhost on the given port. Returns fd or -1. */
static int connect_to_localhost(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(port),
    };
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/** @brief Send a newline-terminated JSON string. */
static bool send_json(int fd, const char *json) {
    size_t len = strlen(json);
    if (write(fd, json, len) != (ssize_t)len) return false;
    if (json[len - 1] != '\n') {
        if (write(fd, "\n", 1) != 1) return false;
    }
    return true;
}

/** @brief Sleep for a short time to let the I/O thread process. */
static void short_sleep(void) {
    usleep(200000); /* 200ms */
}

/* ----------------------------------------------------------------------- */
/* Test: start and stop                                                      */
/* ----------------------------------------------------------------------- */

static int test_start_stop(void) {
    edit_cmd_ring_t cmd_ring, resp_ring;
    ASSERT_TRUE(edit_cmd_ring_init(&cmd_ring, 64, 1024));
    ASSERT_TRUE(edit_cmd_ring_init(&resp_ring, 64, 1024));

    edit_io_thread_t io;
    memset(&io, 0, sizeof(io));

    /* Use port 0 to let OS assign an ephemeral port. */
    ASSERT_TRUE(edit_io_start(&io, 0, &cmd_ring, &resp_ring));
    ASSERT_TRUE(io.port > 0);
    ASSERT_TRUE(io.listen_fd >= 0);

    short_sleep();
    edit_io_stop(&io);

    edit_cmd_ring_destroy(&cmd_ring);
    edit_cmd_ring_destroy(&resp_ring);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: client connect and disconnect                                       */
/* ----------------------------------------------------------------------- */

static int test_client_connect(void) {
    edit_cmd_ring_t cmd_ring, resp_ring;
    ASSERT_TRUE(edit_cmd_ring_init(&cmd_ring, 64, 1024));
    ASSERT_TRUE(edit_cmd_ring_init(&resp_ring, 64, 1024));

    edit_io_thread_t io;
    memset(&io, 0, sizeof(io));
    ASSERT_TRUE(edit_io_start(&io, 0, &cmd_ring, &resp_ring));

    int client_fd = connect_to_localhost(io.port);
    ASSERT_TRUE(client_fd >= 0);

    short_sleep();
    close(client_fd);
    short_sleep();

    edit_io_stop(&io);
    edit_cmd_ring_destroy(&cmd_ring);
    edit_cmd_ring_destroy(&resp_ring);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: send JSON command and verify it appears in cmd_ring                  */
/* ----------------------------------------------------------------------- */

static int test_send_command(void) {
    edit_cmd_ring_t cmd_ring, resp_ring;
    ASSERT_TRUE(edit_cmd_ring_init(&cmd_ring, 64, 4096));
    ASSERT_TRUE(edit_cmd_ring_init(&resp_ring, 64, 4096));

    edit_io_thread_t io;
    memset(&io, 0, sizeof(io));
    ASSERT_TRUE(edit_io_start(&io, 0, &cmd_ring, &resp_ring));

    int client_fd = connect_to_localhost(io.port);
    ASSERT_TRUE(client_fd >= 0);
    short_sleep();

    /* Send a JSON command. */
    const char *cmd = "{\"id\":1,\"cmd\":\"spawn\",\"args\":{\"type\":\"box\"}}";
    ASSERT_TRUE(send_json(client_fd, cmd));
    short_sleep();

    /* Verify it arrived in the command ring. */
    const edit_cmd_slot_t *slot = edit_cmd_ring_peek(&cmd_ring);
    ASSERT_TRUE(slot != NULL);
    ASSERT_TRUE(slot->payload_len > 0);

    /* The payload should contain the JSON. */
    ASSERT_TRUE(memcmp(slot->payload, "{\"id\":", 6) == 0);
    edit_cmd_ring_advance(&cmd_ring);

    close(client_fd);
    short_sleep();

    edit_io_stop(&io);
    edit_cmd_ring_destroy(&cmd_ring);
    edit_cmd_ring_destroy(&resp_ring);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: response ring sends data back to client                             */
/* ----------------------------------------------------------------------- */

static int test_response_to_client(void) {
    edit_cmd_ring_t cmd_ring, resp_ring;
    ASSERT_TRUE(edit_cmd_ring_init(&cmd_ring, 64, 4096));
    ASSERT_TRUE(edit_cmd_ring_init(&resp_ring, 64, 4096));

    edit_io_thread_t io;
    memset(&io, 0, sizeof(io));
    ASSERT_TRUE(edit_io_start(&io, 0, &cmd_ring, &resp_ring));

    int client_fd = connect_to_localhost(io.port);
    ASSERT_TRUE(client_fd >= 0);
    short_sleep();

    /* Send a command to get the client registered. */
    const char *cmd = "{\"id\":1,\"cmd\":\"ping\"}\n";
    ASSERT_TRUE(send_json(client_fd, cmd));
    short_sleep();

    /* Drain the command from cmd_ring. */
    edit_cmd_ring_advance(&cmd_ring);

    /* Push a response into the response ring. */
    const char *resp = "{\"id\":1,\"ok\":true}\n";
    ASSERT_TRUE(edit_cmd_ring_push(&resp_ring, 0, resp, (uint32_t)strlen(resp)));
    short_sleep();

    /* Read the response from the client socket. */
    char buf[512];
    /* Set a timeout so we don't block forever. */
    struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    ASSERT_TRUE(n > 0);
    buf[n] = '\0';

    /* Verify we got the response JSON. */
    ASSERT_TRUE(strstr(buf, "\"ok\":true") != NULL);

    close(client_fd);
    short_sleep();

    edit_io_stop(&io);
    edit_cmd_ring_destroy(&cmd_ring);
    edit_cmd_ring_destroy(&resp_ring);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: multiple clients                                                    */
/* ----------------------------------------------------------------------- */

static int test_multiple_clients(void) {
    edit_cmd_ring_t cmd_ring, resp_ring;
    ASSERT_TRUE(edit_cmd_ring_init(&cmd_ring, 64, 4096));
    ASSERT_TRUE(edit_cmd_ring_init(&resp_ring, 64, 4096));

    edit_io_thread_t io;
    memset(&io, 0, sizeof(io));
    ASSERT_TRUE(edit_io_start(&io, 0, &cmd_ring, &resp_ring));

    int fd1 = connect_to_localhost(io.port);
    int fd2 = connect_to_localhost(io.port);
    ASSERT_TRUE(fd1 >= 0);
    ASSERT_TRUE(fd2 >= 0);
    short_sleep();

    /* Both clients send commands. */
    send_json(fd1, "{\"id\":1,\"cmd\":\"move\"}");
    send_json(fd2, "{\"id\":2,\"cmd\":\"rotate\"}");
    short_sleep();

    /* Both should appear in cmd_ring. */
    uint32_t count = edit_cmd_ring_count(&cmd_ring);
    ASSERT_TRUE(count >= 2);

    close(fd1);
    close(fd2);
    short_sleep();

    edit_io_stop(&io);
    edit_cmd_ring_destroy(&cmd_ring);
    edit_cmd_ring_destroy(&resp_ring);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: partial line buffering                                              */
/* ----------------------------------------------------------------------- */

static int test_partial_line(void) {
    edit_cmd_ring_t cmd_ring, resp_ring;
    ASSERT_TRUE(edit_cmd_ring_init(&cmd_ring, 64, 4096));
    ASSERT_TRUE(edit_cmd_ring_init(&resp_ring, 64, 4096));

    edit_io_thread_t io;
    memset(&io, 0, sizeof(io));
    ASSERT_TRUE(edit_io_start(&io, 0, &cmd_ring, &resp_ring));

    int client_fd = connect_to_localhost(io.port);
    ASSERT_TRUE(client_fd >= 0);
    short_sleep();

    /* Send JSON in two parts (no newline in first part). */
    write(client_fd, "{\"id\":3,\"cm", 11);
    short_sleep();
    write(client_fd, "d\":\"test\"}\n", 11);
    short_sleep();

    /* Should arrive as one complete command. */
    const edit_cmd_slot_t *slot = edit_cmd_ring_peek(&cmd_ring);
    ASSERT_TRUE(slot != NULL);
    ASSERT_TRUE(slot->payload_len > 0);

    close(client_fd);
    short_sleep();

    edit_io_stop(&io);
    edit_cmd_ring_destroy(&cmd_ring);
    edit_cmd_ring_destroy(&resp_ring);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: NULL params to start                                                */
/* ----------------------------------------------------------------------- */

static int test_null_params(void) {
    ASSERT_FALSE(edit_io_start(NULL, 0, NULL, NULL));
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test runner                                                              */
/* ----------------------------------------------------------------------- */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"start_stop",          test_start_stop},
    {"client_connect",      test_client_connect},
    {"send_command",        test_send_command},
    {"response_to_client",  test_response_to_client},
    {"multiple_clients",    test_multiple_clients},
    {"partial_line",        test_partial_line},
    {"null_params",         test_null_params},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN  %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("OK   %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s (rc=%d)\n", tc->name, rc);
            break;
        }
    }
    printf("\n%zu / %zu tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}

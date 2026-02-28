/**
 * @file client_editor_input_tests.c
 * @brief Tests for editor input → push event conversion.
 */

#define _DEFAULT_SOURCE

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>

#include "ferrum/editor/client/client_state_socket.h"
#include "ferrum/editor/client/client_state_dispatch.h"
#include "ferrum/editor/client/client_cursor.h"
#include "ferrum/editor/client/client_editor_input.h"

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
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    return fd;
}

static bool poll_retry_(client_state_socket_t *css) {
    for (int i = 0; i < 20; i++) {
        if (client_state_socket_poll(css)) return true;
        usleep(5000);
    }
    return false;
}

typedef struct test_ctx {
    client_state_socket_t   css;
    editor_cursor_t         cursor;
    client_state_dispatch_t disp;
    editor_input_t          input;
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
    editor_input_init(&t->input, &t->disp);
    return true;
}

static void teardown_(test_ctx_t *t) {
    close(t->ctrl_fd);
    client_state_socket_destroy(&t->css);
}

/* ----------------------------------------------------------------------- */
/* Tests                                                                     */
/* ----------------------------------------------------------------------- */

/** Click on entity sends entity_clicked. */
static bool test_click_entity(void) {
    test_ctx_t t;
    ASSERT(setup_(&t));

    editor_input_click(&t.input, 42, 1.0f, 2.0f, 3.0f);
    usleep(50000);

    char buf[1024];
    ssize_t n = recv(t.ctrl_fd, buf, sizeof(buf) - 1, 0);
    ASSERT(n > 0);
    buf[n] = '\0';
    ASSERT(strstr(buf, "entity_clicked") != NULL);
    ASSERT(strstr(buf, "42") != NULL);

    teardown_(&t);
    return true;
}

/** Click on empty space sends cursor_moved. */
static bool test_click_empty(void) {
    test_ctx_t t;
    ASSERT(setup_(&t));

    editor_input_click(&t.input, 0, 5.0f, 0.0f, 10.0f);
    usleep(50000);

    char buf[1024];
    ssize_t n = recv(t.ctrl_fd, buf, sizeof(buf) - 1, 0);
    ASSERT(n > 0);
    buf[n] = '\0';
    ASSERT(strstr(buf, "cursor_moved") != NULL);
    ASSERT(strstr(buf, "5") != NULL);

    teardown_(&t);
    return true;
}

/** Right-click sends context_menu. */
static bool test_right_click(void) {
    test_ctx_t t;
    ASSERT(setup_(&t));

    editor_input_right_click(&t.input, 7, 1.0f, 2.0f, 3.0f);
    usleep(50000);

    char buf[1024];
    ssize_t n = recv(t.ctrl_fd, buf, sizeof(buf) - 1, 0);
    ASSERT(n > 0);
    buf[n] = '\0';
    ASSERT(strstr(buf, "context_menu") != NULL);
    ASSERT(strstr(buf, "7") != NULL);

    teardown_(&t);
    return true;
}

/** Box select sends entity list. */
static bool test_box_select(void) {
    test_ctx_t t;
    ASSERT(setup_(&t));

    uint32_t ids[] = {1, 5, 9};
    editor_input_box_select(&t.input, ids, 3);
    usleep(50000);

    char buf[1024];
    ssize_t n = recv(t.ctrl_fd, buf, sizeof(buf) - 1, 0);
    ASSERT(n > 0);
    buf[n] = '\0';
    ASSERT(strstr(buf, "box_select") != NULL);
    ASSERT(strstr(buf, "1") != NULL);
    ASSERT(strstr(buf, "5") != NULL);
    ASSERT(strstr(buf, "9") != NULL);

    teardown_(&t);
    return true;
}

/** Box select with zero count is a no-op. */
static bool test_box_select_empty(void) {
    test_ctx_t t;
    ASSERT(setup_(&t));

    editor_input_box_select(&t.input, NULL, 0);
    usleep(50000);

    char buf[1024];
    ssize_t n = recv(t.ctrl_fd, buf, sizeof(buf) - 1, 0);
    /* Should not have received anything. */
    ASSERT(n < 0);  /* EAGAIN on non-blocking socket. */

    teardown_(&t);
    return true;
}

/** Null input doesn't crash. */
static bool test_null_input(void) {
    editor_input_init(NULL, NULL);
    editor_input_click(NULL, 0, 0, 0, 0);
    editor_input_right_click(NULL, 0, 0, 0, 0);
    editor_input_box_select(NULL, NULL, 0);
    return true;
}

int main(void) {
    RUN(test_click_entity);
    RUN(test_click_empty);
    RUN(test_right_click);
    RUN(test_box_select);
    RUN(test_box_select_empty);
    RUN(test_null_input);

    printf("\n%d / %d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}

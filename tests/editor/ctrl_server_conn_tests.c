/**
 * @file ctrl_server_conn_tests.c
 * @brief Tests for controller TCP connection to editor server.
 *
 * Tests connect via the full editor context (which starts an I/O thread),
 * then send commands and receive responses through the controller connection.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ferrum/editor/ctrl_conn.h"
#include "ferrum/editor/editor_ctx.h"
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

/* ----------------------------------------------------------------------- */
/* Tests                                                                     */
/* ----------------------------------------------------------------------- */

/** Init and disconnect without connecting. */
static bool test_init_disconnect(void) {
    ctrl_conn_t conn;
    ctrl_conn_init(&conn);
    ASSERT(conn.fd == -1);
    ASSERT(conn.state == CTRL_CONN_DISCONNECTED);
    ctrl_conn_disconnect(&conn);
    return true;
}

/** Connect to a running editor server. */
static bool test_connect(void) {
    editor_ctx_t ctx;
    editor_ctx_config_t config = {.edit_port = 0};
    ASSERT(editor_ctx_init(&ctx, &config));

    ctrl_conn_t conn;
    ctrl_conn_init(&conn);
    ASSERT(ctrl_conn_connect(&conn, "127.0.0.1", ctx.io_thread.port));
    ASSERT(conn.state == CTRL_CONN_CONNECTED);
    ASSERT(conn.fd >= 0);

    ctrl_conn_disconnect(&conn);
    editor_ctx_shutdown(&ctx);
    return true;
}

/** Connect to invalid port → error. */
static bool test_connect_fail(void) {
    ctrl_conn_t conn;
    ctrl_conn_init(&conn);
    /* Port 1 should be refused on most systems. */
    ASSERT(!ctrl_conn_connect(&conn, "127.0.0.1", 1));
    ASSERT(conn.state == CTRL_CONN_DISCONNECTED);
    return true;
}

/** Send a command and receive a response. */
static bool test_send_recv(void) {
    editor_ctx_t ctx;
    editor_ctx_config_t config = {.edit_port = 0};
    ASSERT(editor_ctx_init(&ctx, &config));

    ctrl_conn_t conn;
    ctrl_conn_init(&conn);
    ASSERT(ctrl_conn_connect(&conn, "127.0.0.1", ctx.io_thread.port));

    /* Send a spawn command. */
    ASSERT(ctrl_conn_send_cmd(&conn, "spawn"));

    /* Wait for I/O thread to receive. */
    usleep(200000);

    /* Drain on the server side. */
    uint32_t processed = editor_tick_drain(&ctx);
    ASSERT(processed == 1);

    /* Wait for I/O thread to send response. */
    usleep(200000);

    /* Read response. */
    ASSERT(ctrl_conn_recv(&conn));

    char line[4096];
    uint32_t len = ctrl_conn_pop_line(&conn, line, sizeof(line));
    ASSERT(len > 0);
    ASSERT(strstr(line, "\"ok\":true") != NULL);

    ctrl_conn_disconnect(&conn);
    editor_ctx_shutdown(&ctx);
    return true;
}

/** Send raw JSON command. */
static bool test_send_raw(void) {
    editor_ctx_t ctx;
    editor_ctx_config_t config = {.edit_port = 0};
    ASSERT(editor_ctx_init(&ctx, &config));

    ctrl_conn_t conn;
    ctrl_conn_init(&conn);
    ASSERT(ctrl_conn_connect(&conn, "127.0.0.1", ctx.io_thread.port));

    const char *json = "{\"id\":42,\"cmd\":\"spawn\","
                       "\"args\":{\"type\":\"box\",\"pos\":[1,2,3]}}";
    ASSERT(ctrl_conn_send_raw(&conn, json, (uint32_t)strlen(json)));

    usleep(200000);
    editor_tick_drain(&ctx);
    usleep(200000);

    ASSERT(ctrl_conn_recv(&conn));
    char line[4096];
    uint32_t len = ctrl_conn_pop_line(&conn, line, sizeof(line));
    ASSERT(len > 0);
    ASSERT(strstr(line, "\"id\":42") != NULL);

    ctrl_conn_disconnect(&conn);
    editor_ctx_shutdown(&ctx);
    return true;
}

/** Pop line with no complete line returns 0. */
static bool test_pop_line_empty(void) {
    ctrl_conn_t conn;
    ctrl_conn_init(&conn);
    char buf[256];
    ASSERT(ctrl_conn_pop_line(&conn, buf, sizeof(buf)) == 0);
    return true;
}

/** Multiple responses accumulated and popped individually. */
static bool test_multiple_responses(void) {
    editor_ctx_t ctx;
    editor_ctx_config_t config = {.edit_port = 0};
    ASSERT(editor_ctx_init(&ctx, &config));

    ctrl_conn_t conn;
    ctrl_conn_init(&conn);
    ASSERT(ctrl_conn_connect(&conn, "127.0.0.1", ctx.io_thread.port));

    /* Send 3 spawn commands. */
    const char *j1 = "{\"id\":1,\"cmd\":\"spawn\",\"args\":{\"type\":\"box\"}}";
    const char *j2 = "{\"id\":2,\"cmd\":\"spawn\",\"args\":{\"type\":\"box\"}}";
    const char *j3 = "{\"id\":3,\"cmd\":\"spawn\",\"args\":{\"type\":\"box\"}}";
    ctrl_conn_send_raw(&conn, j1, (uint32_t)strlen(j1));
    ctrl_conn_send_raw(&conn, j2, (uint32_t)strlen(j2));
    ctrl_conn_send_raw(&conn, j3, (uint32_t)strlen(j3));

    usleep(200000);
    uint32_t processed = editor_tick_drain(&ctx);
    ASSERT(processed == 3);
    usleep(200000);

    /* Read all available data. */
    ctrl_conn_recv(&conn);

    /* Pop 3 lines. */
    char line[4096];
    int count = 0;
    while (ctrl_conn_pop_line(&conn, line, sizeof(line)) > 0) {
        count++;
        ASSERT(strstr(line, "\"ok\":true") != NULL);
    }
    ASSERT(count == 3);

    ctrl_conn_disconnect(&conn);
    editor_ctx_shutdown(&ctx);
    return true;
}

/** Null params. */
static bool test_null_params(void) {
    ctrl_conn_init(NULL);  /* Should not crash. */
    ctrl_conn_disconnect(NULL);
    ASSERT(!ctrl_conn_connect(NULL, NULL, 0));
    ASSERT(!ctrl_conn_send_cmd(NULL, NULL));
    ASSERT(!ctrl_conn_send_raw(NULL, NULL, 0));
    ASSERT(!ctrl_conn_recv(NULL));
    ASSERT(ctrl_conn_pop_line(NULL, NULL, 0) == 0);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Main                                                                      */
/* ----------------------------------------------------------------------- */

int main(void) {
    RUN(test_init_disconnect);
    RUN(test_connect);
    RUN(test_connect_fail);
    RUN(test_send_recv);
    RUN(test_send_raw);
    RUN(test_pop_line_empty);
    RUN(test_multiple_responses);
    RUN(test_null_params);

    printf("\n%d / %d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}

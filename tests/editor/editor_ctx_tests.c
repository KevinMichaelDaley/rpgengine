/**
 * @file editor_ctx_tests.c
 * @brief Tests for editor context initialization, shutdown, and tick drain.
 */

#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "ferrum/editor/editor_ctx.h"
#include "ferrum/editor/edit_entity.h"
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
/* Tests                                                                     */
/* ----------------------------------------------------------------------- */

/** Init with defaults and immediately shut down. */
static bool test_init_shutdown(void) {
    editor_ctx_t ctx;
    editor_ctx_config_t config = {0};
    ASSERT(editor_ctx_init(&ctx, &config));
    ASSERT(ctx.initialized);
    editor_ctx_shutdown(&ctx);
    ASSERT(!ctx.initialized);
    return true;
}

/** Init with custom config values. */
static bool test_init_custom_config(void) {
    editor_ctx_t ctx;
    editor_ctx_config_t config = {
        .max_entities = 128,
        .undo_capacity = 64,
        .ring_capacity = 256,
        .ring_payload_max = 4096,
        .dispatch_arena = 16384,
    };
    ASSERT(editor_ctx_init(&ctx, &config));
    ASSERT(ctx.config.max_entities == 128);
    ASSERT(ctx.config.undo_capacity == 64);
    editor_ctx_shutdown(&ctx);
    return true;
}

/** Double shutdown should be safe (no crash). */
static bool test_double_shutdown(void) {
    editor_ctx_t ctx;
    editor_ctx_config_t config = {0};
    ASSERT(editor_ctx_init(&ctx, &config));
    editor_ctx_shutdown(&ctx);
    editor_ctx_shutdown(&ctx);  /* Should not crash. */
    return true;
}

/** Drain with no pending commands returns 0. */
static bool test_drain_empty(void) {
    editor_ctx_t ctx;
    editor_ctx_config_t config = {0};
    ASSERT(editor_ctx_init(&ctx, &config));
    ASSERT(editor_tick_drain(&ctx) == 0);
    editor_ctx_shutdown(&ctx);
    return true;
}

/** Push a spawn command into the ring manually and drain it. */
static bool test_drain_spawn(void) {
    editor_ctx_t ctx;
    editor_ctx_config_t config = {0};
    ASSERT(editor_ctx_init(&ctx, &config));

    /* Directly push a command into the ring (simulating I/O thread). */
    const char *cmd = "{\"id\":1,\"cmd\":\"spawn\","
                      "\"args\":{\"type\":\"box\",\"pos\":[1,2,3]}}";
    ASSERT(edit_cmd_ring_push(&ctx.cmd_ring, 1, cmd, (uint32_t)strlen(cmd)));

    /* Drain should process exactly 1 command. */
    uint32_t processed = editor_tick_drain(&ctx);
    ASSERT(processed == 1);

    /* Entity should exist. */
    ASSERT(edit_entity_store_count(&ctx.entities) == 1);
    const edit_entity_t *e = edit_entity_store_get(&ctx.entities, 0);
    ASSERT(e != NULL);
    ASSERT_NEAR(e->pos[0], 1.0f, 0.001);
    ASSERT_NEAR(e->pos[1], 2.0f, 0.001);
    ASSERT_NEAR(e->pos[2], 3.0f, 0.001);

    /* Response should be in the response ring. */
    ASSERT(!edit_cmd_ring_empty(&ctx.resp_ring));

    editor_ctx_shutdown(&ctx);
    return true;
}

/** Push multiple commands and drain all at once. */
static bool test_drain_multiple(void) {
    editor_ctx_t ctx;
    editor_ctx_config_t config = {0};
    ASSERT(editor_ctx_init(&ctx, &config));

    const char *cmd1 = "{\"id\":1,\"cmd\":\"spawn\","
                       "\"args\":{\"type\":\"box\"}}";
    const char *cmd2 = "{\"id\":2,\"cmd\":\"spawn\","
                       "\"args\":{\"type\":\"sphere\"}}";
    const char *cmd3 = "{\"id\":3,\"cmd\":\"spawn\","
                       "\"args\":{\"type\":\"box\"}}";
    edit_cmd_ring_push(&ctx.cmd_ring, 1, cmd1, (uint32_t)strlen(cmd1));
    edit_cmd_ring_push(&ctx.cmd_ring, 2, cmd2, (uint32_t)strlen(cmd2));
    edit_cmd_ring_push(&ctx.cmd_ring, 3, cmd3, (uint32_t)strlen(cmd3));

    uint32_t processed = editor_tick_drain(&ctx);
    ASSERT(processed == 3);
    ASSERT(edit_entity_store_count(&ctx.entities) == 3);

    editor_ctx_shutdown(&ctx);
    return true;
}

/** End-to-end: connect via TCP, send command, drain, verify response. */
static bool test_tcp_roundtrip(void) {
    editor_ctx_t ctx;
    editor_ctx_config_t config = {.edit_port = 0}; /* ephemeral port */
    ASSERT(editor_ctx_init(&ctx, &config));

    uint16_t port = ctx.io_thread.port;
    ASSERT(port > 0);

    /* Connect via TCP. */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT(sock >= 0);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
    };
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    ASSERT(connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0);

    /* Send a spawn command. */
    const char *cmd = "{\"id\":1,\"cmd\":\"spawn\","
                      "\"args\":{\"type\":\"box\",\"pos\":[7,8,9]}}\n";
    send(sock, cmd, strlen(cmd), 0);

    /* Wait for I/O thread to receive and push to ring. */
    usleep(200000);

    /* Drain the command. */
    uint32_t processed = editor_tick_drain(&ctx);
    ASSERT(processed == 1);

    /* Verify entity. */
    const edit_entity_t *e = edit_entity_store_get(&ctx.entities, 0);
    ASSERT(e != NULL);
    ASSERT_NEAR(e->pos[0], 7.0f, 0.001);

    /* Wait for I/O thread to send back the response. */
    usleep(200000);

    /* Read response from TCP. */
    char resp[4096] = {0};
    ssize_t n = recv(sock, resp, sizeof(resp) - 1, 0);
    ASSERT(n > 0);
    ASSERT(strstr(resp, "\"ok\":true") != NULL);

    close(sock);
    editor_ctx_shutdown(&ctx);
    return true;
}

/** Null parameters should not crash. */
static bool test_null_params(void) {
    ASSERT(!editor_ctx_init(NULL, NULL));
    editor_ctx_shutdown(NULL);
    ASSERT(editor_tick_drain(NULL) == 0);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Main                                                                      */
/* ----------------------------------------------------------------------- */

int main(void) {
    RUN(test_init_shutdown);
    RUN(test_init_custom_config);
    RUN(test_double_shutdown);
    RUN(test_drain_empty);
    RUN(test_drain_spawn);
    RUN(test_drain_multiple);
    RUN(test_tcp_roundtrip);
    RUN(test_null_params);

    printf("\n%d / %d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}

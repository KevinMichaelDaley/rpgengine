/**
 * @file p200d_scene_connection_tests.c
 * @brief Unit tests for scene editor server connection module.
 *
 * Tests connection state management, command formatting, and line
 * extraction. Does NOT test actual socket I/O (no server needed).
 * Headless — tests the data layer only.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/editor/scene/scene_connection.h"

/* ---- Test harness ---- */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__,      \
                    __LINE__, #cond);                                          \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                \
    do {                                                                        \
        if ((exp) != (act)) {                                                  \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got "   \
                    "%d\n", __FILE__, __LINE__, (int)(exp), (int)(act));        \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_STR_EQ(exp, act)                                                \
    do {                                                                        \
        if (strcmp((exp), (act)) != 0) {                                        \
            fprintf(stderr, "ASSERT_STR_EQ failed: %s:%d: expected \"%s\" "   \
                    "got \"%s\"\n", __FILE__, __LINE__, (exp), (act));          \
            return 1;                                                          \
        }                                                                      \
    } while (0)

/* ---- Tests ---- */

/**
 * Test basic init and destroy.
 */
static int test_scene_conn_init(void) {
    scene_connection_t conn;
    scene_conn_config_t cfg = {0};
    cfg.host = "127.0.0.1";
    cfg.tcp_port = 7777;
    cfg.udp_port = 7778;

    bool ok = scene_connection_init(&conn, &cfg);
    ASSERT_TRUE(ok);
    ASSERT_INT_EQ(SCENE_CONN_DISCONNECTED, (int)conn.state);
    ASSERT_TRUE(conn.tcp.fd == -1);

    scene_connection_destroy(&conn);
    return 0;
}

/**
 * Test destroy is safe on NULL and double-call.
 */
static int test_scene_conn_destroy_safe(void) {
    scene_connection_t conn;
    scene_conn_config_t cfg = {0};
    cfg.host = "127.0.0.1";
    cfg.tcp_port = 7777;
    cfg.udp_port = 7778;

    scene_connection_init(&conn, &cfg);
    scene_connection_destroy(&conn);
    /* Double destroy should not crash */
    scene_connection_destroy(&conn);
    /* NULL should not crash */
    scene_connection_destroy(NULL);
    return 0;
}

/**
 * Test init with NULL config returns false.
 */
static int test_scene_conn_init_null(void) {
    scene_connection_t conn;
    bool ok = scene_connection_init(&conn, NULL);
    ASSERT_TRUE(!ok);
    return 0;
}

/**
 * Test that init with NULL host returns false.
 */
static int test_scene_conn_init_no_host(void) {
    scene_connection_t conn;
    scene_conn_config_t cfg = {0};
    cfg.tcp_port = 7777;
    cfg.udp_port = 7778;
    /* host is NULL */

    bool ok = scene_connection_init(&conn, &cfg);
    ASSERT_TRUE(!ok);
    return 0;
}

/**
 * Test status string for disconnected state.
 */
static int test_scene_conn_status_disconnected(void) {
    scene_connection_t conn;
    scene_conn_config_t cfg = {0};
    cfg.host = "127.0.0.1";
    cfg.tcp_port = 7777;
    cfg.udp_port = 7778;

    scene_connection_init(&conn, &cfg);

    char buf[128];
    scene_connection_format_status(&conn, buf, sizeof(buf));
    ASSERT_TRUE(strstr(buf, "Offline") != NULL ||
                strstr(buf, "Disconnected") != NULL);

    scene_connection_destroy(&conn);
    return 0;
}

/**
 * Test that config is stored correctly.
 */
static int test_scene_conn_config_stored(void) {
    scene_connection_t conn;
    scene_conn_config_t cfg = {0};
    cfg.host = "192.168.1.100";
    cfg.tcp_port = 9000;
    cfg.udp_port = 9001;

    scene_connection_init(&conn, &cfg);

    ASSERT_INT_EQ(9000, (int)conn.config.tcp_port);
    ASSERT_INT_EQ(9001, (int)conn.config.udp_port);
    ASSERT_STR_EQ("192.168.1.100", conn.config.host);

    scene_connection_destroy(&conn);
    return 0;
}

/**
 * Test send on disconnected connection returns false.
 */
static int test_scene_conn_send_disconnected(void) {
    scene_connection_t conn;
    scene_conn_config_t cfg = {0};
    cfg.host = "127.0.0.1";
    cfg.tcp_port = 7777;
    cfg.udp_port = 7778;

    scene_connection_init(&conn, &cfg);

    bool sent = scene_connection_send_cmd(&conn, "spawn box");
    ASSERT_TRUE(!sent);

    scene_connection_destroy(&conn);
    return 0;
}

/**
 * Test pump on disconnected connection is safe (returns 0).
 */
static int test_scene_conn_pump_disconnected(void) {
    scene_connection_t conn;
    scene_conn_config_t cfg = {0};
    cfg.host = "127.0.0.1";
    cfg.tcp_port = 7777;
    cfg.udp_port = 7778;

    scene_connection_init(&conn, &cfg);

    int events = scene_connection_pump(&conn);
    ASSERT_INT_EQ(0, events);

    scene_connection_destroy(&conn);
    return 0;
}

/**
 * Test pop_response on empty connection returns 0.
 */
static int test_scene_conn_pop_empty(void) {
    scene_connection_t conn;
    scene_conn_config_t cfg = {0};
    cfg.host = "127.0.0.1";
    cfg.tcp_port = 7777;
    cfg.udp_port = 7778;

    scene_connection_init(&conn, &cfg);

    char buf[256];
    uint32_t len = scene_connection_pop_response(&conn, buf, sizeof(buf));
    ASSERT_INT_EQ(0, (int)len);

    scene_connection_destroy(&conn);
    return 0;
}

/**
 * Test connection status transitions.
 */
static int test_scene_conn_state_transitions(void) {
    scene_connection_t conn;
    scene_conn_config_t cfg = {0};
    cfg.host = "127.0.0.1";
    cfg.tcp_port = 7777;
    cfg.udp_port = 7778;

    scene_connection_init(&conn, &cfg);
    ASSERT_INT_EQ(SCENE_CONN_DISCONNECTED, (int)conn.state);

    /* Simulate error state */
    conn.state = SCENE_CONN_ERROR;
    char buf[128];
    scene_connection_format_status(&conn, buf, sizeof(buf));
    ASSERT_TRUE(strstr(buf, "Error") != NULL);

    scene_connection_destroy(&conn);
    return 0;
}

/**
 * Test next_cmd_id increments properly.
 */
static int test_scene_conn_cmd_id(void) {
    scene_connection_t conn;
    scene_conn_config_t cfg = {0};
    cfg.host = "127.0.0.1";
    cfg.tcp_port = 7777;
    cfg.udp_port = 7778;

    scene_connection_init(&conn, &cfg);

    uint32_t id1 = scene_connection_next_id(&conn);
    uint32_t id2 = scene_connection_next_id(&conn);
    ASSERT_INT_EQ(1, (int)id1);
    ASSERT_INT_EQ(2, (int)id2);

    scene_connection_destroy(&conn);
    return 0;
}

/**
 * Test status format with pending commands count.
 */
static int test_scene_conn_status_pending(void) {
    scene_connection_t conn;
    scene_conn_config_t cfg = {0};
    cfg.host = "127.0.0.1";
    cfg.tcp_port = 7777;
    cfg.udp_port = 7778;

    scene_connection_init(&conn, &cfg);

    /* Simulate connected with pending commands */
    conn.state = SCENE_CONN_CONNECTED;
    conn.pending_cmds = 3;

    char buf[128];
    scene_connection_format_status(&conn, buf, sizeof(buf));
    ASSERT_TRUE(strstr(buf, "Syncing") != NULL);
    ASSERT_TRUE(strstr(buf, "3") != NULL);

    /* No pending = synced */
    conn.pending_cmds = 0;
    scene_connection_format_status(&conn, buf, sizeof(buf));
    ASSERT_TRUE(strstr(buf, "Synced") != NULL);

    scene_connection_destroy(&conn);
    return 0;
}

/* ---- Test runner ---- */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"scene_conn_init",              test_scene_conn_init},
    {"scene_conn_destroy_safe",      test_scene_conn_destroy_safe},
    {"scene_conn_init_null",         test_scene_conn_init_null},
    {"scene_conn_init_no_host",      test_scene_conn_init_no_host},
    {"scene_conn_status_disconnected", test_scene_conn_status_disconnected},
    {"scene_conn_config_stored",     test_scene_conn_config_stored},
    {"scene_conn_send_disconnected", test_scene_conn_send_disconnected},
    {"scene_conn_pump_disconnected", test_scene_conn_pump_disconnected},
    {"scene_conn_pop_empty",         test_scene_conn_pop_empty},
    {"scene_conn_state_transitions", test_scene_conn_state_transitions},
    {"scene_conn_cmd_id",            test_scene_conn_cmd_id},
    {"scene_conn_status_pending",    test_scene_conn_status_pending},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;

    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN  %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("  OK %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s\n", tc->name);
            break;
        }
    }

    printf("\n%zu / %zu tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}

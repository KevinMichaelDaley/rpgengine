#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/net/replication/body_state.h"
#include "ferrum/net/replication/body_state_inbox.h"
#include "ferrum/net/rudp/peer.h"
#include "ferrum/net/test_client.h"
#include "ferrum/net/test_clock.h"
#include "ferrum/net/test_link.h"
#include "ferrum/net/udp_socket.h"

#define ASSERT_TRUE(cond)                                                                                \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                                          \
    do {                                                                                                 \
        if ((exp) != (act)) {                                                                            \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n", __FILE__, __LINE__,     \
                    (int)(exp), (int)(act));                                                             \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_UINT_EQ(exp, act)                                                                         \
    do {                                                                                                 \
        if ((uint64_t)(exp) != (uint64_t)(act)) {                                                         \
            fprintf(stderr, "ASSERT_UINT_EQ failed: %s:%d: expected %llu got %llu\n", __FILE__,          \
                    __LINE__, (unsigned long long)(exp), (unsigned long long)(act));                     \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static int sendto_link_(void *io_user, const net_udp_addr_t *to, const void *data, size_t size) {
    (void)to;
    net_test_link_t *link = (net_test_link_t *)io_user;
    if (!link || !data) {
        return -1;
    }
    return (net_test_link_send(link, data, size) == NET_TEST_LINK_OK) ? 0 : -1;
}

static int test_body_state_newest_wins_drops_older_ticks(void) {
    net_test_clock_t clock;
    net_test_clock_init(&clock, 0u);

    /* Ensure the later (newer) state arrives first. */
    net_test_step_t down_steps[] = {
        {1u, 5u, 0u},
        {1u, 0u, 0u},
    };

    net_test_link_t down;
    ASSERT_INT_EQ(NET_TEST_LINK_OK, net_test_link_init(&down, &clock, down_steps, ARRAY_SIZE(down_steps), 8u, NET_RUDP_MAX_PACKET_SIZE));

    net_test_step_t up_steps[] = {{1u, 0u, 0u}};
    net_test_link_t up;
    ASSERT_INT_EQ(NET_TEST_LINK_OK, net_test_link_init(&up, &clock, up_steps, ARRAY_SIZE(up_steps), 8u, NET_RUDP_MAX_PACKET_SIZE));

    net_udp_addr_t dummy;
    ASSERT_INT_EQ(NET_UDP_SOCKET_OK, net_udp_addr_ipv4(&dummy, 127, 0, 0, 1, 40000));

    fr_test_client_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.protocol_id = NET_RUDP_PROTOCOL_ID_P008;
    cfg.tx_link = &up;
    cfg.rx_link = &down;
    cfg.remote_addr = dummy;

    fr_test_client_t *cl = fr_test_client_create(&cfg);
    ASSERT_TRUE(cl != NULL);

    fr_body_state_inbox_t inbox;
    ASSERT_TRUE(fr_body_state_inbox_init(&inbox, 32u));

    net_rudp_peer_t server_peer;
    net_rudp_send_slot_t server_slots[NET_RUDP_SEND_SLOTS_DEFAULT];
    memset(server_slots, 0, sizeof(server_slots));
    net_rudp_peer_init_with_storage(&server_peer, NET_RUDP_PROTOCOL_ID_P008, 50u, server_slots, ARRAY_SIZE(server_slots));

    const uint16_t body_id = 7u;

    net_repl_body_state_t st_old;
    memset(&st_old, 0, sizeof(st_old));
    st_old.server_tick = 2u;
    st_old.body_id = body_id;
    st_old.pos_mm = (net_repl_vec3_mm_t){100, 0, 0};
    st_old.rot_x = 0.0f;
    st_old.rot_y = 0.0f;
    st_old.rot_z = 0.0f;
    st_old.rot_w = 1.0f;

    net_repl_body_state_t st_new;
    memset(&st_new, 0, sizeof(st_new));
    st_new.server_tick = 3u;
    st_new.body_id = body_id;
    st_new.pos_mm = (net_repl_vec3_mm_t){200, 0, 0};
    st_new.rot_x = 0.0f;
    st_new.rot_y = 0.0f;
    st_new.rot_z = 0.0f;
    st_new.rot_w = 1.0f;

    uint8_t payload_old[NET_REPL_BODY_STATE_PAYLOAD_SIZE];
    uint8_t payload_new[NET_REPL_BODY_STATE_PAYLOAD_SIZE];
    ASSERT_INT_EQ(NET_REPL_OK, net_repl_body_state_encode(&st_old, payload_old, sizeof(payload_old)));
    ASSERT_INT_EQ(NET_REPL_OK, net_repl_body_state_encode(&st_new, payload_new, sizeof(payload_new)));

    /* Send older first, newer second; link script delays the older.
       Result: client receives newer tick first, then older tick later. */
    ASSERT_INT_EQ(NET_RUDP_OK,
                  net_rudp_peer_send_unreliable_via(&server_peer,
                                                   &down,
                                                   sendto_link_,
                                                   &dummy,
                                                   0u,
                                                   NET_REPL_SCHEMA_BODY_STATE,
                                                   payload_old,
                                                   sizeof(payload_old)));

    ASSERT_INT_EQ(NET_RUDP_OK,
                  net_rudp_peer_send_unreliable_via(&server_peer,
                                                   &down,
                                                   sendto_link_,
                                                   &dummy,
                                                   0u,
                                                   NET_REPL_SCHEMA_BODY_STATE,
                                                   payload_new,
                                                   sizeof(payload_new)));

    /* Pump at t=0: only the newer tick arrives. */
    ASSERT_TRUE(fr_test_client_pump_rx(cl, 0u));

    uint16_t schema = 0u;
    uint8_t msg[NET_REPL_BODY_STATE_PAYLOAD_SIZE];
    size_t msg_len = sizeof(msg);
    ASSERT_TRUE(fr_test_client_pop_unreliable(cl, &schema, msg, &msg_len));
    ASSERT_UINT_EQ(NET_REPL_SCHEMA_BODY_STATE, schema);
    ASSERT_UINT_EQ(sizeof(payload_new), msg_len);

    ASSERT_TRUE(fr_body_state_inbox_push(&inbox, msg, msg_len));

    net_repl_body_state_t got;
    memset(&got, 0, sizeof(got));
    ASSERT_TRUE(fr_body_state_inbox_get(&inbox, body_id, &got));
    ASSERT_UINT_EQ(3u, got.server_tick);
    ASSERT_INT_EQ(200, got.pos_mm.x_mm);

    /* Advance to deliver the delayed older tick. */
    net_test_clock_advance(&clock, 5u);
    ASSERT_TRUE(fr_test_client_pump_rx(cl, 0u));

    schema = 0u;
    msg_len = sizeof(msg);
    ASSERT_TRUE(fr_test_client_pop_unreliable(cl, &schema, msg, &msg_len));
    ASSERT_UINT_EQ(NET_REPL_SCHEMA_BODY_STATE, schema);
    ASSERT_UINT_EQ(sizeof(payload_old), msg_len);

    ASSERT_TRUE(!fr_body_state_inbox_push(&inbox, msg, msg_len));

    memset(&got, 0, sizeof(got));
    ASSERT_TRUE(fr_body_state_inbox_get(&inbox, body_id, &got));
    ASSERT_UINT_EQ(3u, got.server_tick);
    ASSERT_INT_EQ(200, got.pos_mm.x_mm);

    fr_body_state_inbox_destroy(&inbox);
    fr_test_client_destroy(cl);
    net_test_link_destroy(&up);
    net_test_link_destroy(&down);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"body_state_newest_wins_drops_older_ticks", test_body_state_newest_wins_drops_older_ticks},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0u;
    for (size_t i = 0u; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("PASS %s\n", tc->name);
            passed++;
        } else {
            printf("FAIL %s\n", tc->name);
            return 1;
        }
    }
    printf("%zu/%zu tests passed\n", passed, total);
    return 0;
}

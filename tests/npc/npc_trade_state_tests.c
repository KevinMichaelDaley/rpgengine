/**
 * @file npc_trade_state_tests.c
 * @brief Barter state machine tests: transitions, combat exit, timeout, broadcast.
 *
 * Covers:
 * - BARTER_NONE -> PROPOSED via npc_trade_init
 * - Double-init rejection (already in trade)
 * - SELL/BUY in PROPOSED/ACTIVE phase
 * - PROPOSED -> ACTIVE auto-transition when both parties set items
 * - ACCEPT -> BARTER_RESOLVED
 * - REJECT -> BARTER_RESOLVED
 * - Combat exit -> BARTER_NONE for both
 * - Timeout detection via npc_trade_is_timed_out
 * - Broadcast return code for SELL/BUY outside trade
 * - Prompt generation
 */

#include "ferrum/npc/npc_trade.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RUN(fn) do { printf("  %-48s ", #fn); fn(); } while (0)
#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        printf("FAIL (%s:%d)\n", __FILE__, __LINE__); \
        g_fail++; \
        return; \
    } \
} while (0)
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define PASS() do { printf("PASS\n"); g_pass++; } while (0)

static int g_pass = 0;
static int g_fail = 0;

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static uint64_t fake_now(void) { return 1000000; }

/* ------------------------------------------------------------------ */
/* State machine transition tests                                     */
/* ------------------------------------------------------------------ */

static void test_barter_state_init(void) {
    npc_barter_state_t s;
    npc_barter_state_init(&s);
    ASSERT_EQ(s.phase, BARTER_NONE);
    ASSERT_EQ(s.counter_party_id, (uint64_t)0);
    ASSERT_EQ(s.timeout_deadline_us, (uint64_t)0);
    ASSERT_EQ(s.my_offer_item_id, (uint32_t)0);
    ASSERT_EQ(s.my_ask_item_id, (uint32_t)0);
    ASSERT_EQ(s.their_offer_item_id, (uint32_t)0);
    ASSERT_EQ(s.their_ask_item_id, (uint32_t)0);
    PASS();
}

static void test_trade_init_none_to_proposed(void) {
    npc_barter_state_t a, b;
    npc_barter_state_init(&a);
    npc_barter_state_init(&b);

    uint64_t now = fake_now();
    int err = npc_trade_init(&a, &b, 1, 2, now);
    ASSERT_EQ(err, NPC_TRADE_OK);
    ASSERT_EQ(a.phase, BARTER_PROPOSED);
    ASSERT_EQ(b.phase, BARTER_PROPOSED);
    ASSERT_EQ(a.counter_party_id, (uint64_t)2);
    ASSERT_EQ(b.counter_party_id, (uint64_t)1);
    ASSERT_EQ(a.timeout_deadline_us, now + NPC_TRADE_TIMEOUT_SECONDS * 1000000ULL);
    ASSERT_EQ(b.timeout_deadline_us, now + NPC_TRADE_TIMEOUT_SECONDS * 1000000ULL);
    PASS();
}

static void test_trade_init_already_in_trade(void) {
    npc_barter_state_t a, b, c;
    npc_barter_state_init(&a);
    npc_barter_state_init(&b);
    npc_barter_state_init(&c);

    uint64_t now = fake_now();
    int err = npc_trade_init(&a, &b, 1, 2, now);
    ASSERT_EQ(err, NPC_TRADE_OK);

    /* Try to init 'a' again with 'c' while 'a' is already PROPOSED. */
    err = npc_trade_init(&a, &c, 1, 3, now);
    ASSERT_EQ(err, NPC_TRADE_ERR_ALREADY);
    PASS();
}

static void test_trade_init_target_already_in_trade(void) {
    npc_barter_state_t a, b, c;
    npc_barter_state_init(&a);
    npc_barter_state_init(&b);
    npc_barter_state_init(&c);

    uint64_t now = fake_now();
    /* b and c are already trading. */
    int err = npc_trade_init(&b, &c, 2, 3, now);
    ASSERT_EQ(err, NPC_TRADE_OK);

    /* Try to init 'a' with 'b' while 'b' is already trading. */
    err = npc_trade_init(&a, &b, 1, 2, now);
    ASSERT_EQ(err, NPC_TRADE_ERR_TARGET_BUSY);
    PASS();
}

/* ------------------------------------------------------------------ */
/* SELL / BUY tests                                                    */
/* ------------------------------------------------------------------ */

static void test_trade_sell_in_trade(void) {
    npc_barter_state_t a, b;
    npc_barter_state_init(&a);
    npc_barter_state_init(&b);

    uint64_t now = fake_now();
    int err = npc_trade_init(&a, &b, 1, 2, now);
    ASSERT_EQ(err, NPC_TRADE_OK);

    err = npc_trade_sell(&a, &b, 100);
    ASSERT_EQ(err, NPC_TRADE_OK);
    ASSERT_EQ(a.my_offer_item_id, (uint32_t)100);
    ASSERT_EQ(b.their_offer_item_id, (uint32_t)100);
    PASS();
}

static void test_trade_buy_in_trade(void) {
    npc_barter_state_t a, b;
    npc_barter_state_init(&a);
    npc_barter_state_init(&b);

    uint64_t now = fake_now();
    int err = npc_trade_init(&a, &b, 1, 2, now);
    ASSERT_EQ(err, NPC_TRADE_OK);

    err = npc_trade_buy(&a, &b, 200);
    ASSERT_EQ(err, NPC_TRADE_OK);
    ASSERT_EQ(a.my_ask_item_id, (uint32_t)200);
    ASSERT_EQ(b.their_ask_item_id, (uint32_t)200);
    PASS();
}

static void test_trade_sell_outside_trade(void) {
    npc_barter_state_t a;
    npc_barter_state_init(&a);

    /* Not in a trade — should return broadcast signal. */
    int err = npc_trade_sell(&a, NULL, 100);
    ASSERT_EQ(err, NPC_TRADE_BROADCAST);
    PASS();
}

static void test_trade_buy_outside_trade(void) {
    npc_barter_state_t a;
    npc_barter_state_init(&a);

    int err = npc_trade_buy(&a, NULL, 200);
    ASSERT_EQ(err, NPC_TRADE_BROADCAST);
    PASS();
}

static void test_trade_proposed_to_active(void) {
    npc_barter_state_t a, b;
    npc_barter_state_init(&a);
    npc_barter_state_init(&b);

    uint64_t now = fake_now();
    int err = npc_trade_init(&a, &b, 1, 2, now);
    ASSERT_EQ(err, NPC_TRADE_OK);
    ASSERT_EQ(a.phase, BARTER_PROPOSED);
    ASSERT_EQ(b.phase, BARTER_PROPOSED);

    /* A sets offer, B sets ask — should auto-transition to ACTIVE. */
    err = npc_trade_sell(&a, &b, 100);
    ASSERT_EQ(err, NPC_TRADE_OK);
    ASSERT_EQ(a.phase, BARTER_PROPOSED);

    err = npc_trade_buy(&b, &a, 200);
    ASSERT_EQ(err, NPC_TRADE_OK);
    ASSERT_EQ(a.phase, BARTER_ACTIVE);
    ASSERT_EQ(b.phase, BARTER_ACTIVE);
    PASS();
}

/* ------------------------------------------------------------------ */
/* ACCEPT / REJECT tests                                               */
/* ------------------------------------------------------------------ */

static void test_trade_accept_resolves(void) {
    npc_barter_state_t a, b;
    npc_barter_state_init(&a);
    npc_barter_state_init(&b);

    uint64_t now = fake_now();
    int err = npc_trade_init(&a, &b, 1, 2, now);
    ASSERT_EQ(err, NPC_TRADE_OK);

    /* Set items then accept. */
    npc_trade_sell(&a, &b, 100);
    npc_trade_buy(&b, &a, 200);
    ASSERT_EQ(a.phase, BARTER_ACTIVE);

    err = npc_trade_accept(&a, &b);
    ASSERT_EQ(err, NPC_TRADE_OK);
    ASSERT_EQ(a.phase, BARTER_RESOLVED);
    ASSERT_EQ(b.phase, BARTER_RESOLVED);
    PASS();
}

static void test_trade_accept_not_active(void) {
    npc_barter_state_t a, b;
    npc_barter_state_init(&a);
    npc_barter_state_init(&b);

    uint64_t now = fake_now();
    npc_trade_init(&a, &b, 1, 2, now);

    /* Still PROPOSED, no items set. */
    int err = npc_trade_accept(&a, &b);
    ASSERT_EQ(err, NPC_TRADE_ERR_STATE);
    PASS();
}

static void test_trade_reject_resolves(void) {
    npc_barter_state_t a, b;
    npc_barter_state_init(&a);
    npc_barter_state_init(&b);

    uint64_t now = fake_now();
    int err = npc_trade_init(&a, &b, 1, 2, now);
    ASSERT_EQ(err, NPC_TRADE_OK);

    err = npc_trade_reject(&a, &b);
    ASSERT_EQ(err, NPC_TRADE_OK);
    ASSERT_EQ(a.phase, BARTER_RESOLVED);
    ASSERT_EQ(b.phase, BARTER_RESOLVED);
    PASS();
}

/* ------------------------------------------------------------------ */
/* Combat exit                                                         */
/* ------------------------------------------------------------------ */

static void test_trade_combat_exit(void) {
    npc_barter_state_t a, b;
    npc_barter_state_init(&a);
    npc_barter_state_init(&b);

    uint64_t now = fake_now();
    int err = npc_trade_init(&a, &b, 1, 2, now);
    ASSERT_EQ(err, NPC_TRADE_OK);
    ASSERT_EQ(a.phase, BARTER_PROPOSED);

    npc_trade_combat_exit(&a, &b);
    ASSERT_EQ(a.phase, BARTER_NONE);
    ASSERT_EQ(b.phase, BARTER_NONE);
    ASSERT_EQ(a.counter_party_id, (uint64_t)0);
    ASSERT_EQ(b.counter_party_id, (uint64_t)0);
    PASS();
}

static void test_trade_combat_exit_in_active(void) {
    npc_barter_state_t a, b;
    npc_barter_state_init(&a);
    npc_barter_state_init(&b);

    uint64_t now = fake_now();
    npc_trade_init(&a, &b, 1, 2, now);
    npc_trade_sell(&a, &b, 100);
    npc_trade_buy(&b, &a, 200);
    ASSERT_EQ(a.phase, BARTER_ACTIVE);

    npc_trade_combat_exit(&a, &b);
    ASSERT_EQ(a.phase, BARTER_NONE);
    ASSERT_EQ(b.phase, BARTER_NONE);
    PASS();
}

/* ------------------------------------------------------------------ */
/* Timeout                                                             */
/* ------------------------------------------------------------------ */

static void test_trade_timeout_not_expired(void) {
    npc_barter_state_t a, b;
    npc_barter_state_init(&a);
    npc_barter_state_init(&b);

    uint64_t now = fake_now();
    npc_trade_init(&a, &b, 1, 2, now);

    /* Just after init. */
    ASSERT_TRUE(!npc_trade_is_timed_out(&a, now + 1000));
    ASSERT_TRUE(!npc_trade_is_timed_out(&b, now + 1000));
    PASS();
}

static void test_trade_timeout_expired(void) {
    npc_barter_state_t a, b;
    npc_barter_state_init(&a);
    npc_barter_state_init(&b);

    uint64_t now = fake_now();
    npc_trade_init(&a, &b, 1, 2, now);

    uint64_t deadline = now + NPC_TRADE_TIMEOUT_SECONDS * 1000000ULL;
    ASSERT_TRUE(!npc_trade_is_timed_out(&a, deadline - 1));
    ASSERT_TRUE(npc_trade_is_timed_out(&a, deadline));
    ASSERT_TRUE(npc_trade_is_timed_out(&a, deadline + 1000));
    PASS();
}

static void test_trade_timeout_inactive_in_resolved(void) {
    npc_barter_state_t a, b;
    npc_barter_state_init(&a);
    npc_barter_state_init(&b);

    uint64_t now = fake_now();
    npc_trade_init(&a, &b, 1, 2, now);
    npc_trade_reject(&a, &b);
    ASSERT_EQ(a.phase, BARTER_RESOLVED);

    uint64_t deadline = now + NPC_TRADE_TIMEOUT_SECONDS * 1000000ULL;
    ASSERT_TRUE(!npc_trade_is_timed_out(&a, deadline));
    PASS();
}

/* ------------------------------------------------------------------ */
/* Prompt generation                                                   */
/* ------------------------------------------------------------------ */

static void test_trade_prompt(void) {
    npc_barter_state_t a;
    npc_barter_state_init(&a);

    char buf[256];
    int len = npc_trade_prompt(&a, buf, sizeof(buf), "Bob");
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(buf, "Bob") != NULL);
    ASSERT_TRUE(strstr(buf, "none") != NULL);
    PASS();
}

static void test_trade_prompt_with_items(void) {
    npc_barter_state_t a, b;
    npc_barter_state_init(&a);
    npc_barter_state_init(&b);

    uint64_t now = fake_now();
    npc_trade_init(&a, &b, 1, 2, now);
    npc_trade_sell(&a, &b, 1);
    npc_trade_buy(&a, &b, 2);

    const char *names[] = { "sword", "shield" };
    char buf[256];
    int len = npc_trade_prompt_fmt(&a, buf, sizeof(buf), "Alice", 2, names);
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(buf, "Alice") != NULL);
    ASSERT_TRUE(strstr(buf, "sword") != NULL);
    ASSERT_TRUE(strstr(buf, "shield") != NULL);
    PASS();
}

/* ------------------------------------------------------------------ */
/* Error string                                                        */
/* ------------------------------------------------------------------ */

static void test_trade_error_str(void) {
    ASSERT_TRUE(strcmp(npc_trade_error_str(NPC_TRADE_OK), "ok") == 0);
    ASSERT_TRUE(strlen(npc_trade_error_str(NPC_TRADE_ERR_ALREADY)) > 0);
    ASSERT_TRUE(strlen(npc_trade_error_str(NPC_TRADE_ERR_NO_TARGET)) > 0);
    ASSERT_TRUE(strlen(npc_trade_error_str(-999)) > 0);
    PASS();
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("=== NPC Trade State Tests ===\n\n");

    RUN(test_barter_state_init);
    RUN(test_trade_init_none_to_proposed);
    RUN(test_trade_init_already_in_trade);
    RUN(test_trade_init_target_already_in_trade);
    RUN(test_trade_sell_in_trade);
    RUN(test_trade_buy_in_trade);
    RUN(test_trade_sell_outside_trade);
    RUN(test_trade_buy_outside_trade);
    RUN(test_trade_proposed_to_active);
    RUN(test_trade_accept_resolves);
    RUN(test_trade_accept_not_active);
    RUN(test_trade_reject_resolves);
    RUN(test_trade_combat_exit);
    RUN(test_trade_combat_exit_in_active);
    RUN(test_trade_timeout_not_expired);
    RUN(test_trade_timeout_expired);
    RUN(test_trade_timeout_inactive_in_resolved);
    RUN(test_trade_prompt);
    RUN(test_trade_prompt_with_items);
    RUN(test_trade_error_str);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "ferrum/net/stream.h"

static int test_out_of_order_and_duplicates(void) {
    fr_rudp_stream_config_t cfg = {0};
    fr_rudp_stream_t *s = fr_rudp_stream_create(&cfg);
    assert(s != NULL);

    /* Frame format: 2-byte seq LE + 2-byte chan LE + payload */
    uint8_t f2[4 + 3] = {2, 0, 0, 0, 'b', 'b', 'b'};
    uint8_t f1[4 + 3] = {1, 0, 0, 0, 'a', 'a', 'a'};
    uint8_t f2_dup[4 + 2] = {2, 0, 0, 0, 'x', 'x'};

    /* Push out-of-order: 2 then 1. */
    assert(fr_rudp_stream_push_frame(s, f2, sizeof f2));
    assert(fr_rudp_stream_push_frame(s, f1, sizeof f1));
    /* Duplicate of 2 should be suppressed (push returns false or true is acceptable
       depending on policy; the test expects no duplicate delivery). */
    (void)fr_rudp_stream_push_frame(s, f2_dup, sizeof f2_dup);

    /* Pop in-order: first sequence 1 (payload 'aaa'), then 2 ('bbb'). */
    uint8_t out[8] = {0};
    size_t len = sizeof out;
    int ok = fr_rudp_stream_pop(s, 0u, out, &len);
    assert(ok);
    assert(len == 3);
    assert(out[0] == 'a' && out[1] == 'a' && out[2] == 'a');

    memset(out, 0, sizeof out);
    len = sizeof out;
    ok = fr_rudp_stream_pop(s, 0u, out, &len);
    assert(ok);
    assert(len == 3);
    assert(out[0] == 'b' && out[1] == 'b' && out[2] == 'b');

    /* No more messages: pop should fail. */
    len = sizeof out;
    ok = fr_rudp_stream_pop(s, 0u, out, &len);
    assert(!ok);

    fr_rudp_stream_destroy(s);
    return 0;
}

int main(void) {
    int rc = 0;
    rc |= test_out_of_order_and_duplicates();
    printf("p007_net_stream_api_tests: OK\n");
    return rc;
}

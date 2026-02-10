#include <stdlib.h>
#include <string.h>

#include "ferrum/net/replication/body_state_inbox.h"

bool fr_body_state_inbox_init(fr_body_state_inbox_t *inbox, uint16_t max_bodies) {
    if (!inbox || max_bodies == 0u) {
        return false;
    }

    memset(inbox, 0, sizeof(*inbox));
    inbox->max_bodies = max_bodies;

    inbox->states = (net_repl_body_state_t *)calloc((size_t)max_bodies, sizeof(net_repl_body_state_t));
    inbox->last_server_tick = (uint16_t *)calloc((size_t)max_bodies, sizeof(uint16_t));
    inbox->has_state = (uint8_t *)calloc((size_t)max_bodies, sizeof(uint8_t));

    if (!inbox->states || !inbox->last_server_tick || !inbox->has_state) {
        fr_body_state_inbox_destroy(inbox);
        return false;
    }

    return true;
}

void fr_body_state_inbox_destroy(fr_body_state_inbox_t *inbox) {
    if (!inbox) {
        return;
    }

    free(inbox->states);
    free(inbox->last_server_tick);
    free(inbox->has_state);

    memset(inbox, 0, sizeof(*inbox));
}

bool fr_body_state_inbox_push(fr_body_state_inbox_t *inbox, const uint8_t *payload, size_t payload_size) {
    if (!inbox || !inbox->states || !inbox->last_server_tick || !inbox->has_state || !payload) {
        return false;
    }

    net_repl_body_state_t st;
    if (net_repl_body_state_decode(&st, payload, payload_size) != NET_REPL_OK) {
        return false;
    }

    if (st.body_id >= inbox->max_bodies) {
        return false;
    }

    if (!inbox->has_state[st.body_id]) {
        inbox->states[st.body_id] = st;
        inbox->last_server_tick[st.body_id] = st.server_tick;
        inbox->has_state[st.body_id] = 1u;
        return true;
    }

    if (st.server_tick <= inbox->last_server_tick[st.body_id]) {
        return false;
    }

    inbox->states[st.body_id] = st;
    inbox->last_server_tick[st.body_id] = st.server_tick;
    return true;
}

bool fr_body_state_inbox_get(const fr_body_state_inbox_t *inbox, uint16_t body_id, net_repl_body_state_t *out_state) {
    if (!inbox || !out_state || !inbox->states || !inbox->has_state) {
        return false;
    }

    if (body_id >= inbox->max_bodies || !inbox->has_state[body_id]) {
        return false;
    }

    *out_state = inbox->states[body_id];
    return true;
}

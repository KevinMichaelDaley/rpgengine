#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/net/replication/body_state.h"
#include "ferrum/server/physics/net/body_state_broadcast.h"

struct fr_server_body_state_broadcast {
    fr_server_body_state_broadcast_config_t cfg;
};

static double round_half_away_from_zero_f64_(double x) {
    if (x >= 0.0) {
        return floor(x + 0.5);
    }
    return ceil(x - 0.5);
}

static int32_t quantize_mm_i32_sat_(float meters) {
    const double mm = (double)meters * 1000.0;
    if (!isfinite(mm)) {
        return 0;
    }

    const double r = round_half_away_from_zero_f64_(mm);
    if (r > (double)INT32_MAX) {
        return INT32_MAX;
    }
    if (r < (double)INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)r;
}

static int16_t quantize_i16_sat_(float v, float scale) {
    const double x = (double)v * (double)scale;
    if (!isfinite(x)) {
        return 0;
    }

    const double r = round_half_away_from_zero_f64_(x);
    if (r > (double)INT16_MAX) {
        return INT16_MAX;
    }
    if (r < (double)INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)r;
}

static uint16_t tier_interval_ticks_(uint8_t tier) {
    if (tier > 5u) {
        tier = 5u;
    }
    return (uint16_t)(1u << tier);
}

fr_server_body_state_broadcast_t *fr_server_body_state_broadcast_create(const fr_server_body_state_broadcast_config_t *cfg) {
    if (!cfg || cfg->max_clients == 0u || !cfg->world || !cfg->get_client_out_topics_cb) {
        return NULL;
    }

    fr_server_body_state_broadcast_t *b = (fr_server_body_state_broadcast_t *)calloc(1u, sizeof(*b));
    if (!b) {
        return NULL;
    }
    b->cfg = *cfg;
    return b;
}

void fr_server_body_state_broadcast_destroy(fr_server_body_state_broadcast_t *b) {
    free(b);
}

bool fr_server_body_state_broadcast_tick(fr_server_body_state_broadcast_t *b,
                                        uint16_t server_tick,
                                        uint64_t now_ms) {
    if (!b || !b->cfg.world) {
        return false;
    }

    phys_world_t *world = b->cfg.world;
    const uint32_t cap = world->body_pool.capacity;

    for (uint32_t body_idx = 0u; body_idx < cap; ++body_idx) {
        if (!phys_body_pool_is_active(&world->body_pool, body_idx)) {
            continue;
        }
        phys_body_t *body = phys_body_pool_get_curr(&world->body_pool, body_idx);
        if (!body) {
            continue;
        }
        if (body_idx > (uint32_t)UINT16_MAX) {
            continue;
        }

        uint8_t tier = body->tier;
        const uint16_t interval = tier_interval_ticks_(tier);
        if (interval != 0u && (uint16_t)(server_tick % interval) != 0u) {
            continue;
        }
        if (tier > 5u) {
            tier = 5u;
        }

        net_repl_body_state_t st;
        memset(&st, 0, sizeof(st));
        st.server_tick = server_tick;
        st.body_id = (uint16_t)body_idx;
        st.pos_mm.x_mm = quantize_mm_i32_sat_(body->position.x);
        st.pos_mm.y_mm = quantize_mm_i32_sat_(body->position.y);
        st.pos_mm.z_mm = quantize_mm_i32_sat_(body->position.z);
        st.rot_x = body->orientation.x;
        st.rot_y = body->orientation.y;
        st.rot_z = body->orientation.z;
        st.rot_w = body->orientation.w;
        st.vel_x_mm_s = quantize_i16_sat_(body->linear_vel.x, 1000.0f);
        st.vel_y_mm_s = quantize_i16_sat_(body->linear_vel.y, 1000.0f);
        st.vel_z_mm_s = quantize_i16_sat_(body->linear_vel.z, 1000.0f);
        st.ang_x_mrad_s = quantize_i16_sat_(body->angular_vel.x, 1000.0f);
        st.ang_y_mrad_s = quantize_i16_sat_(body->angular_vel.y, 1000.0f);
        st.ang_z_mrad_s = quantize_i16_sat_(body->angular_vel.z, 1000.0f);
        st.send_time_ms = (uint32_t)now_ms;
        st.flags = net_repl_body_state_set_tier(0u, tier);

        uint8_t payload[NET_REPL_BODY_STATE_PAYLOAD_SIZE];
        if (net_repl_body_state_encode(&st, payload, sizeof(payload)) != NET_REPL_OK) {
            continue;
        }

        uint8_t msg[2u + NET_REPL_BODY_STATE_PAYLOAD_SIZE];
        msg[0] = (uint8_t)(NET_REPL_SCHEMA_BODY_STATE & 0xFFu);
        msg[1] = (uint8_t)((NET_REPL_SCHEMA_BODY_STATE >> 8u) & 0xFFu);
        memcpy(msg + 2u, payload, sizeof(payload));

        for (uint16_t client_id = 0u; client_id < b->cfg.max_clients; ++client_id) {
            fr_topic_channel_t *out_rel = NULL;
            fr_topic_channel_t *out_unrel = NULL;
            if (!b->cfg.get_client_out_topics_cb(b->cfg.io_user, client_id, &out_rel, &out_unrel)) {
                continue;
            }
            if (!out_unrel) {
                continue;
            }
            (void)fr_topic_channel_push(out_unrel, msg, sizeof(msg));
        }
    }

    return true;
}

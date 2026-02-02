#include <math.h>
#include <stdlib.h>

#include "ferrum/net/quantization.h"
#include "ferrum/net/replication/state_cube.h"
#include "internal.h"

static void send_state_job(void *user_data) {
    struct server_repl_send_job_ctx *ctx = (struct server_repl_send_job_ctx *)user_data;
    server_repl_server_t *srv = ctx->srv;
    uint16_t ci = ctx->client_index;
    uint16_t ei = ctx->entity_index;

    if (!srv->clients[ci].active || !srv->clients[ci].joined || !srv->entities[ei].active) {
        return;
    }

    const float t = (float)srv->server_tick / (float)srv->cfg.tick_hz;
    const float phase = (float)srv->entities[ei].owner_client_id * 0.25f;
    vec3_t pos = (vec3_t){cosf(t + phase), 0.0f, sinf(t + phase)};
    quat_t rot = (quat_t){0.0f, 0.0f, 0.0f, 1.0f};

    net_qvec3_mm_t qpos;
    net_qquat_snorm16_t qrot;
    if (net_quantize_vec3_mm(pos, &qpos) != NET_QUANT_OK) {
        return;
    }
    if (net_quantize_quat_snorm16(rot, &qrot) != NET_QUANT_OK) {
        return;
    }

    net_repl_state_cube_t st;
    st.server_tick = srv->server_tick;
    st.entity_id = srv->entities[ei].entity_id;
    st.pos_mm = (net_repl_vec3_mm_t){qpos.x_mm, qpos.y_mm, qpos.z_mm};
    st.rot_snorm16 = (net_repl_quat_snorm16_t){qrot.x, qrot.y, qrot.z, qrot.w};

    uint8_t payload[NET_REPL_STATE_CUBE_PAYLOAD_SIZE];
    if (net_repl_state_cube_encode(&st, payload, sizeof(payload)) != NET_REPL_OK) {
        return;
    }

    (void)net_rudp_peer_send_unreliable(&srv->clients[ci].peer,
                                        srv->sock,
                                        &srv->clients[ci].addr,
                                        ctx->now_ms,
                                        NET_REPL_SCHEMA_STATE_CUBE,
                                        payload,
                                        sizeof(payload));
}

int server_repl_server_tick(server_repl_server_t *srv, uint64_t now_ms) {
    if (!srv) {
        return SERVER_REPL_ERR_INVALID;
    }

    for (uint16_t ci = 0u; ci < srv->cfg.max_clients; ++ci) {
        if (!srv->clients[ci].active || !srv->clients[ci].joined) {
            continue;
        }
        (void)net_rudp_peer_tick_resend(&srv->clients[ci].peer, srv->sock, &srv->clients[ci].addr, now_ms);
    }

    const size_t max_jobs = (size_t)srv->cfg.max_clients * (size_t)srv->cfg.max_entities;
    struct server_repl_send_job_ctx *ctxs = srv->send_job_ctxs;
    uint8_t ctxs_owned = 0u;
    if (!ctxs) {
        ctxs = (struct server_repl_send_job_ctx *)calloc(max_jobs, sizeof(*ctxs));
        ctxs_owned = 1u;
    }

    if (ctxs) {
        size_t job_count = 0u;
        for (uint16_t ci = 0u; ci < srv->cfg.max_clients; ++ci) {
            if (!srv->clients[ci].active || !srv->clients[ci].joined) {
                continue;
            }
            for (uint16_t ei = 0u; ei < srv->cfg.max_entities; ++ei) {
                if (!srv->entities[ei].active) {
                    continue;
                }
                if (job_count >= max_jobs) {
                    break;
                }
                ctxs[job_count] = (struct server_repl_send_job_ctx){srv, ci, ei, now_ms};
                (void)job_dispatch(srv->jobs, send_state_job, &ctxs[job_count], 0, NULL);
                job_count++;
            }
        }
        (void)job_system_wait_idle(srv->jobs);
        if (ctxs_owned) {
            free(ctxs);
        }
    }

    srv->server_tick = (uint16_t)(srv->server_tick + 1u);
    return SERVER_REPL_OK;
}

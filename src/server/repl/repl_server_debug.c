#include <string.h>
#include "internal.h"

int server_repl_server_debug_force_client_joined(server_repl_server_t *srv, uint16_t client_id) {
    if (!srv || client_id >= srv->cfg.max_clients) {
        return SERVER_REPL_ERR_INVALID;
    }
    srv->clients[client_id].active = 1u;
    srv->clients[client_id].joined = 1u;
    srv->clients[client_id].welcome_sent = 0u;
    return SERVER_REPL_OK;
}

int server_repl_server_debug_add_active_entity(server_repl_server_t *srv, uint16_t owner_client_id, uint32_t entity_id_hint, uint16_t *out_index) {
    if (!srv || owner_client_id >= srv->cfg.max_clients) {
        return SERVER_REPL_ERR_INVALID;
    }
    for (uint16_t i = 0u; i < srv->cfg.max_entities; ++i) {
        if (!srv->entities[i].active) {
            srv->entities[i].active = 1u;
            srv->entities[i].owner_client_id = owner_client_id;
            srv->entities[i].entity_id = (entity_id_hint != 0u) ? entity_id_hint : srv->next_entity_id++;
            if (out_index) { *out_index = i; }
            return SERVER_REPL_OK;
        }
    }
    return SERVER_REPL_ERR_INVALID;
}

int server_repl_server_debug_force_entity_known(server_repl_server_t *srv, uint16_t client_id, uint16_t entity_index) {
    if (!srv || client_id >= srv->cfg.max_clients || entity_index >= srv->cfg.max_entities) {
        return SERVER_REPL_ERR_INVALID;
    }
    if (!srv->entity_known_bits || srv->entity_known_stride_bytes == 0u) {
        return SERVER_REPL_ERR_INVALID;
    }
    uint8_t *known = srv->entity_known_bits + ((size_t)client_id * srv->entity_known_stride_bytes);
    size_t byte_index = (size_t)entity_index >> 3u;
    uint8_t mask = (uint8_t)(1u << (entity_index & 7u));
    known[byte_index] = (uint8_t)(known[byte_index] | mask);
    return SERVER_REPL_OK;
}

static void debug_noop(void *user) { (void)user; }

int server_repl_server_debug_schedule_state_job(server_repl_server_t *srv, uint16_t client_id, uint16_t entity_index, uint64_t now_ms) {
    if (!srv || !srv->jobs || !srv->send_job_ctxs) {
        return SERVER_REPL_ERR_INVALID;
    }
    if (client_id >= srv->cfg.max_clients || entity_index >= srv->cfg.max_entities) {
        return SERVER_REPL_ERR_INVALID;
    }
    size_t idx = (size_t)client_id * (size_t)srv->cfg.max_entities + (size_t)entity_index;
    struct server_repl_send_job_ctx *ctx = &srv->send_job_ctxs[idx];
    ctx->srv = srv;
    ctx->client_index = client_id;
    ctx->entity_index = entity_index;
    ctx->now_ms = now_ms;
    if (job_dispatch(srv->jobs, debug_noop, ctx, 0, NULL) == JOB_ID_INVALID) {
        return SERVER_REPL_ERR_INVALID;
    }
    srv->stats.state_jobs_scheduled++;
    return SERVER_REPL_OK;
}

#include <stdlib.h>
#include <string.h>

#include "internal.h"

static size_t bitset_stride_bytes(uint16_t bits) {
    return ((size_t)bits + 7u) / 8u;
}

server_repl_server_t *server_repl_server_create(const server_repl_config_t *cfg,
                                                net_udp_socket_t *sock,
                                                job_system_t *jobs) {
    if (!cfg || !sock || !jobs || cfg->max_clients == 0u || cfg->tick_hz == 0u || cfg->max_entities == 0u) {
        return NULL;
    }

    server_repl_server_t *srv = (server_repl_server_t *)calloc(1u, sizeof(*srv));
    if (!srv) {
        return NULL;
    }
    srv->cfg = *cfg;
    if (srv->cfg.rudp_send_slots_per_client == 0u) {
        srv->cfg.rudp_send_slots_per_client = (size_t)srv->cfg.max_entities + 8u;
    }

    size_t total_rudp_slots = (size_t)srv->cfg.max_clients * (size_t)srv->cfg.rudp_send_slots_per_client;
    size_t want_rudp_bytes = net_rudp_send_slot_storage_size(total_rudp_slots);
    if (!srv->cfg.rudp_send_slot_storage || srv->cfg.rudp_send_slot_storage_bytes < want_rudp_bytes) {
        free(srv);
        return NULL;
    }

    memset(srv->cfg.rudp_send_slot_storage, 0, want_rudp_bytes);
    srv->sock = sock;
    srv->jobs = jobs;
    srv->server_tick = 0u;
    srv->next_entity_id = 1u;

    srv->clients_owned = 0u;
    srv->entities_owned = 0u;
    srv->entity_known_owned = 0u;
    srv->spawn_cursor_owned = 0u;
    srv->state_cursor_owned = 0u;
    srv->send_job_ctxs = NULL;
    srv->send_job_ctx_capacity = 0u;
    srv->send_job_ctxs_owned = 0u;
    srv->spawn_batch_entries = NULL;
    srv->spawn_batch_entity_indices = NULL;
    srv->spawn_batch_entry_capacity = 0u;
    srv->spawn_batch_owned = 0u;
    srv->entity_known_bits = NULL;
    srv->entity_known_stride_bytes = 0u;
    srv->spawn_cursor = NULL;
    srv->state_cursor = NULL;

    size_t want_clients_bytes = server_repl_client_storage_size(cfg->max_clients);
    if (cfg->client_storage && cfg->client_storage_bytes >= want_clients_bytes) {
        srv->clients = (struct server_repl_client *)cfg->client_storage;
        memset(srv->clients, 0, want_clients_bytes);
    } else {
        srv->clients = (struct server_repl_client *)calloc((size_t)cfg->max_clients, sizeof(*srv->clients));
        srv->clients_owned = 1u;
    }

    size_t want_entities_bytes = server_repl_entity_storage_size(cfg->max_entities);
    if (cfg->entity_storage && cfg->entity_storage_bytes >= want_entities_bytes) {
        srv->entities = (struct server_repl_entity *)cfg->entity_storage;
        memset(srv->entities, 0, want_entities_bytes);
    } else {
        srv->entities = (struct server_repl_entity *)calloc((size_t)cfg->max_entities, sizeof(*srv->entities));
        srv->entities_owned = 1u;
    }

    size_t want_ctx_bytes = server_repl_send_job_ctx_storage_size(cfg->max_clients, cfg->max_entities);
    if (cfg->send_job_ctx_storage && cfg->send_job_ctx_storage_bytes >= want_ctx_bytes) {
        srv->send_job_ctxs = (struct server_repl_send_job_ctx *)cfg->send_job_ctx_storage;
        srv->send_job_ctx_capacity = (size_t)cfg->max_clients * (size_t)cfg->max_entities;
        memset(srv->send_job_ctxs, 0, want_ctx_bytes);
    } else {
        srv->send_job_ctxs = (struct server_repl_send_job_ctx *)calloc(1u, want_ctx_bytes);
        if (!srv->send_job_ctxs) {
            server_repl_server_destroy(srv);
            return NULL;
        }
        srv->send_job_ctx_capacity = (size_t)cfg->max_clients * (size_t)cfg->max_entities;
        srv->send_job_ctxs_owned = 1u;
    }

    if (!srv->clients || !srv->entities) {
        server_repl_server_destroy(srv);
        return NULL;
    }

    srv->entity_known_stride_bytes = bitset_stride_bytes(cfg->max_entities);
    const size_t known_bytes = (size_t)cfg->max_clients * srv->entity_known_stride_bytes;
    srv->entity_known_bits = (uint8_t *)calloc(1u, known_bytes);
    if (!srv->entity_known_bits) {
        server_repl_server_destroy(srv);
        return NULL;
    }
    srv->entity_known_owned = 1u;

    srv->spawn_cursor = (uint16_t *)calloc((size_t)cfg->max_clients, sizeof(*srv->spawn_cursor));
    if (!srv->spawn_cursor) {
        server_repl_server_destroy(srv);
        return NULL;
    }
    srv->spawn_cursor_owned = 1u;

    srv->state_cursor = (uint16_t *)calloc((size_t)cfg->max_clients, sizeof(*srv->state_cursor));
    if (!srv->state_cursor) {
        server_repl_server_destroy(srv);
        return NULL;
    }
    srv->state_cursor_owned = 1u;

    const size_t max_payload = NET_RUDP_MAX_PACKET_SIZE - NET_PACKET_HEADER_SIZE - 8u;
    const size_t max_entries_z = (max_payload > 4u) ? ((max_payload - 4u) / 18u) : 0u;
    const uint16_t entry_capacity = (uint16_t)((max_entries_z > 0xffffu) ? 0xffffu : max_entries_z);
    if (entry_capacity > 0u) {
        srv->spawn_batch_entries = (net_repl_spawn_batch_entry_t *)calloc((size_t)entry_capacity, sizeof(*srv->spawn_batch_entries));
        srv->spawn_batch_entity_indices = (uint16_t *)calloc((size_t)entry_capacity, sizeof(*srv->spawn_batch_entity_indices));
        if (!srv->spawn_batch_entries || !srv->spawn_batch_entity_indices) {
            server_repl_server_destroy(srv);
            return NULL;
        }
        srv->spawn_batch_entry_capacity = entry_capacity;
        srv->spawn_batch_owned = 1u;
    }

    return srv;
}

void server_repl_server_destroy(server_repl_server_t *srv) {
    if (!srv) {
        return;
    }
    if (srv->clients_owned) {
        free(srv->clients);
    }
    if (srv->entities_owned) {
        free(srv->entities);
    }
    if (srv->entity_known_owned) {
        free(srv->entity_known_bits);
    }
    if (srv->spawn_cursor_owned) {
        free(srv->spawn_cursor);
    }
    if (srv->state_cursor_owned) {
        free(srv->state_cursor);
    }
    if (srv->spawn_batch_owned) {
        free(srv->spawn_batch_entries);
        free(srv->spawn_batch_entity_indices);
    }
    if (srv->send_job_ctxs_owned) {
        free(srv->send_job_ctxs);
    }
    free(srv);
}

server_repl_stats_t server_repl_server_stats(const server_repl_server_t *srv) {
    if (!srv) {
        return (server_repl_stats_t){0};
    }
    return srv->stats;
}

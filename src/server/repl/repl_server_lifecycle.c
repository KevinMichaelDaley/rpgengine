#include <stdlib.h>
#include <string.h>

#include "internal.h"

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
    srv->sock = sock;
    srv->jobs = jobs;
    srv->server_tick = 0u;
    srv->next_entity_id = 1u;

    srv->clients = (struct server_repl_client *)calloc((size_t)cfg->max_clients, sizeof(*srv->clients));
    srv->entities = (struct server_repl_entity *)calloc((size_t)cfg->max_entities, sizeof(*srv->entities));
    if (!srv->clients || !srv->entities) {
        server_repl_server_destroy(srv);
        return NULL;
    }

    return srv;
}

void server_repl_server_destroy(server_repl_server_t *srv) {
    if (!srv) {
        return;
    }
    free(srv->clients);
    free(srv->entities);
    free(srv);
}

server_repl_stats_t server_repl_server_stats(const server_repl_server_t *srv) {
    if (!srv) {
        return (server_repl_stats_t){0};
    }
    return srv->stats;
}

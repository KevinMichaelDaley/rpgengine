#include <stddef.h>
#include <stdint.h>

#include "internal.h"

size_t server_repl_client_storage_size(uint16_t max_clients) {
    return (size_t)max_clients * sizeof(struct server_repl_client);
}

size_t server_repl_entity_storage_size(uint16_t max_entities) {
    return (size_t)max_entities * sizeof(struct server_repl_entity);
}

size_t server_repl_send_job_ctx_storage_size(uint16_t max_clients, uint16_t max_entities) {
    return (size_t)max_clients * (size_t)max_entities * sizeof(struct server_repl_send_job_ctx);
}

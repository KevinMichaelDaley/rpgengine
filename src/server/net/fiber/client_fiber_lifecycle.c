#include <stdlib.h>
#include <string.h>
#include "client_fiber_internal.h"

fr_server_client_fiber_t *fr_server_client_fiber_create(const fr_server_client_fiber_config_t *cfg) {
    if (!cfg) return NULL;
    if (cfg->reliable_channels == 0 || cfg->reliable_slot_count == 0 || cfg->max_payload_size == 0)
        return NULL;

    fr_server_client_fiber_t *f = (fr_server_client_fiber_t*)calloc(1, sizeof(*f));
    if (!f) return NULL;

    fr_rudp_stream_config_t scfg;
    memset(&scfg, 0, sizeof scfg);
    scfg.reliable_channels = cfg->reliable_channels;
    scfg.reliable_slot_count = cfg->reliable_slot_count;
    scfg.max_payload_size = cfg->max_payload_size;
    scfg.topics = cfg->topics;
    scfg.num_topics = cfg->num_topics;

    f->stream = fr_rudp_stream_create(&scfg);
    if (!f->stream) { free(f); return NULL; }
    f->topics = cfg->topics;
    f->num_topics = cfg->num_topics;
    return f;
}

void fr_server_client_fiber_destroy(fr_server_client_fiber_t *fiber) {
    if (!fiber) return;
    if (fiber->stream) fr_rudp_stream_destroy(fiber->stream);
    free(fiber);
}

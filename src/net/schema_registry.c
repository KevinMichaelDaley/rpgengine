#include "ferrum/net/schema_registry.h"

#include "ferrum/net/bit_pack.h"

static int registry_find_index(const net_schema_registry_t *registry, uint16_t schema_id, size_t *out_index) {
    if (!registry || !out_index) {
        return NET_SCHEMA_REGISTRY_ERR_INVALID;
    }

    for (size_t i = 0u; i < registry->count; ++i) {
        if (registry->schema_ids[i] == schema_id) {
            *out_index = i;
            return NET_SCHEMA_REGISTRY_OK;
        }
    }
    return NET_SCHEMA_REGISTRY_ERR_UNKNOWN_SCHEMA;
}

void net_schema_registry_init(net_schema_registry_t *registry) {
    if (!registry) {
        return;
    }
    registry->count = 0u;
}

int net_schema_registry_register(net_schema_registry_t *registry, uint16_t schema_id, size_t payload_size_bytes) {
    if (!registry) {
        return NET_SCHEMA_REGISTRY_ERR_INVALID;
    }
    if (payload_size_bytes > UINT16_MAX) {
        return NET_SCHEMA_REGISTRY_ERR_INVALID;
    }

    size_t existing = 0u;
    int found = registry_find_index(registry, schema_id, &existing);
    if (found == NET_SCHEMA_REGISTRY_OK) {
        registry->payload_sizes[existing] = (uint16_t)payload_size_bytes;
        return NET_SCHEMA_REGISTRY_OK;
    }
    if (found != NET_SCHEMA_REGISTRY_ERR_UNKNOWN_SCHEMA) {
        return found;
    }

    if (registry->count >= (size_t)NET_SCHEMA_REGISTRY_MAX_SCHEMAS) {
        return NET_SCHEMA_REGISTRY_ERR_FULL;
    }

    size_t index = registry->count;
    registry->schema_ids[index] = schema_id;
    registry->payload_sizes[index] = (uint16_t)payload_size_bytes;
    registry->count++;
    return NET_SCHEMA_REGISTRY_OK;
}

int net_schema_registry_decode_packet(const net_schema_registry_t *registry,
                                     const uint8_t *bytes,
                                     size_t size,
                                     uint16_t *out_schema_id,
                                     const uint8_t **out_payload,
                                     size_t *out_payload_size) {
    if (!registry || !bytes || !out_schema_id || !out_payload || !out_payload_size) {
        return NET_SCHEMA_REGISTRY_ERR_INVALID;
    }

    net_bit_pack_header_t header = {0};
    const uint8_t *payload = NULL;
    size_t payload_size = 0u;

    int rc = net_bit_pack_decode(&header, bytes, size, &payload, &payload_size);
    if (rc != NET_BIT_PACK_OK) {
        return NET_SCHEMA_REGISTRY_ERR_PACKET;
    }

    *out_schema_id = header.schema_id;
    *out_payload = payload;
    *out_payload_size = payload_size;

    size_t index = 0u;
    rc = registry_find_index(registry, header.schema_id, &index);
    if (rc != NET_SCHEMA_REGISTRY_OK) {
        return rc;
    }

    uint16_t expected = registry->payload_sizes[index];
    if ((uint16_t)payload_size != expected) {
        return NET_SCHEMA_REGISTRY_ERR_PAYLOAD_LENGTH;
    }

    return NET_SCHEMA_REGISTRY_OK;
}

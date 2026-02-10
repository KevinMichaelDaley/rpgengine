/**
 * @file validation.c
 * @brief Protocol validation and instrumentation counters.
 *
 * Non-static functions: 3 (init, check, reset_stats).
 */

#include "ferrum/net/validation.h"
#include <string.h>

void net_validation_init(net_validation_ctx_t *ctx,
                         uint32_t protocol_id,
                         const uint16_t *schemas,
                         uint32_t schema_count) {
    if (!ctx) { return; }
    memset(ctx, 0, sizeof(*ctx));
    ctx->protocol_id = protocol_id;

    if (schema_count > NET_VALIDATION_MAX_SCHEMAS) {
        schema_count = NET_VALIDATION_MAX_SCHEMAS;
    }
    ctx->schema_count = schema_count;
    if (schemas && schema_count > 0) {
        memcpy(ctx->schemas, schemas, schema_count * sizeof(uint16_t));
    }
}

int net_validation_check(net_validation_ctx_t *ctx,
                         const uint8_t *packet,
                         size_t size) {
    if (!ctx || !packet) { return NET_VALIDATION_ERR_INVALID; }

    ctx->stats.packets_total++;
    ctx->stats.bytes_total += size;

    /* Check 1: minimum packet length. */
    if (size < NET_VALIDATION_MIN_PACKET) {
        ctx->stats.truncated_packets++;
        return NET_VALIDATION_ERR_TRUNCATED;
    }

    /* Check 2: protocol ID (big-endian at offset 0). */
    uint32_t proto = ((uint32_t)packet[0] << 24) |
                     ((uint32_t)packet[1] << 16) |
                     ((uint32_t)packet[2] << 8)  |
                     ((uint32_t)packet[3]);
    if (proto != ctx->protocol_id) {
        ctx->stats.protocol_errors++;
        return NET_VALIDATION_ERR_PROTOCOL;
    }

    /* Check 3: schema ID (big-endian at offset 12). */
    uint16_t schema_id = (uint16_t)((packet[12] << 8) | packet[13]);

    int schema_known = 0;
    for (uint32_t i = 0; i < ctx->schema_count; i++) {
        if (ctx->schemas[i] == schema_id) {
            schema_known = 1;
            break;
        }
    }
    if (!schema_known) {
        ctx->stats.unknown_schemas++;
        return NET_VALIDATION_ERR_SCHEMA;
    }

    /* Check 4: payload size consistency (big-endian at offset 14). */
    uint16_t payload_size = (uint16_t)((packet[14] << 8) | packet[15]);
    if ((size_t)payload_size > size - NET_VALIDATION_MIN_PACKET) {
        ctx->stats.malformed_packets++;
        return NET_VALIDATION_ERR_MALFORMED;
    }

    ctx->stats.packets_valid++;
    return NET_VALIDATION_OK;
}

void net_validation_reset_stats(net_validation_ctx_t *ctx) {
    if (!ctx) { return; }
    memset(&ctx->stats, 0, sizeof(ctx->stats));
}

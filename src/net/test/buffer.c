#include <string.h>

#include "ferrum/net/test_buffer.h"

void net_test_buffer_init(net_test_buffer_t *buffer, uint8_t *storage, size_t capacity) {
    if (!buffer) {
        return;
    }
    buffer->data = storage;
    buffer->capacity = capacity;
    buffer->size = 0u;
    buffer->cursor = 0u;
}

void net_test_buffer_reset(net_test_buffer_t *buffer) {
    if (!buffer) {
        return;
    }
    buffer->size = 0u;
    buffer->cursor = 0u;
}

int net_test_buffer_write(net_test_buffer_t *buffer, const void *data, size_t size) {
    if (!buffer || (!buffer->data && buffer->capacity > 0u) || !data) {
        return NET_TEST_BUFFER_ERR_INVALID;
    }
    if (size == 0u) {
        return NET_TEST_BUFFER_OK;
    }
    if (buffer->size + size > buffer->capacity) {
        return NET_TEST_BUFFER_ERR_OVERFLOW;
    }
    memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
    return NET_TEST_BUFFER_OK;
}

int net_test_buffer_read(net_test_buffer_t *buffer, void *out, size_t size) {
    if (!buffer || !out) {
        return NET_TEST_BUFFER_ERR_INVALID;
    }
    if (size == 0u) {
        return NET_TEST_BUFFER_OK;
    }
    if (buffer->cursor + size > buffer->size) {
        return NET_TEST_BUFFER_ERR_UNDERFLOW;
    }
    memcpy(out, buffer->data + buffer->cursor, size);
    buffer->cursor += size;
    return NET_TEST_BUFFER_OK;
}

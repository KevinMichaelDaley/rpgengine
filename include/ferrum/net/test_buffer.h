#ifndef FERRUM_NET_TEST_BUFFER_H
#define FERRUM_NET_TEST_BUFFER_H

#include <stddef.h>
#include <stdint.h>

/** @file
 * @brief Simple byte buffer helpers for network tests.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Status: operation succeeded. */
#define NET_TEST_BUFFER_OK 0
/** Status: invalid arguments. */
#define NET_TEST_BUFFER_ERR_INVALID -1
/** Status: write exceeds capacity. */
#define NET_TEST_BUFFER_ERR_OVERFLOW -2
/** Status: read exceeds available data. */
#define NET_TEST_BUFFER_ERR_UNDERFLOW -3

/** Byte buffer with explicit cursor and size. */
typedef struct net_test_buffer {
    uint8_t *data;
    size_t capacity;
    size_t size;
    size_t cursor;
} net_test_buffer_t;

/**
 * @brief Initialize the buffer with caller-owned storage.
 * @param buffer Buffer pointer (non-NULL).
 * @param storage Backing storage pointer.
 * @param capacity Storage size in bytes.
 */
void net_test_buffer_init(net_test_buffer_t *buffer, uint8_t *storage, size_t capacity);

/**
 * @brief Reset buffer size and cursor to zero.
 * @param buffer Buffer pointer.
 */
void net_test_buffer_reset(net_test_buffer_t *buffer);

/**
 * @brief Append bytes to the buffer.
 * @param buffer Buffer pointer.
 * @param data Bytes to append.
 * @param size Number of bytes to append.
 * @return NET_TEST_BUFFER_OK on success or error code.
 */
int net_test_buffer_write(net_test_buffer_t *buffer, const void *data, size_t size);

/**
 * @brief Read bytes from the buffer at the current cursor.
 * @param buffer Buffer pointer.
 * @param out Output buffer for bytes.
 * @param size Number of bytes to read.
 * @return NET_TEST_BUFFER_OK on success or error code.
 */
int net_test_buffer_read(net_test_buffer_t *buffer, void *out, size_t size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_TEST_BUFFER_H */

#ifndef FERRUM_NET_REPLICATION_QUAT_SNORM16_H
#define FERRUM_NET_REPLICATION_QUAT_SNORM16_H

#include <stdint.h>

/** @file
 * @brief Small wire-format helper type: quaternion snorm16.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct net_repl_quat_snorm16 {
    int16_t x;
    int16_t y;
    int16_t z;
    int16_t w;
} net_repl_quat_snorm16_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_REPLICATION_QUAT_SNORM16_H */

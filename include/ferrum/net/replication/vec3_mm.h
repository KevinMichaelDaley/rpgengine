#ifndef FERRUM_NET_REPLICATION_VEC3_MM_H
#define FERRUM_NET_REPLICATION_VEC3_MM_H

#include <stdint.h>

/** @file
 * @brief Small wire-format helper type: vec3 in millimeters.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct net_repl_vec3_mm {
    int32_t x_mm;
    int32_t y_mm;
    int32_t z_mm;
} net_repl_vec3_mm_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_REPLICATION_VEC3_MM_H */

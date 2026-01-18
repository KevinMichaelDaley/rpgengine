#ifndef FERRUM_ECS_COMMON_H
#define FERRUM_ECS_COMMON_H

#include <stdint.h>

/** @file
 * @brief Shared ECS status codes and macros.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Status codes for ECS operations. */
typedef enum ecs_status {
    ECS_OK = 0,
    ECS_ERR_OOM = 1,
    ECS_ERR_INVALID = 2,
    ECS_ERR_EXISTS = 3,
    ECS_ERR_NOT_FOUND = 4,
    ECS_ERR_FULL = 5
} ecs_status_t;

/** Sentinel for missing sparse entries. */
#define ECS_SPARSE_INVALID UINT32_MAX

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_ECS_COMMON_H */

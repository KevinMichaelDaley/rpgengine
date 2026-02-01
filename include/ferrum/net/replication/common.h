#ifndef FERRUM_NET_REPLICATION_COMMON_H
#define FERRUM_NET_REPLICATION_COMMON_H

#include <stdint.h>

/** @file
 * @brief Common constants and status codes for replication protocol helpers.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define NET_REPL_OK 0
#define NET_REPL_ERR_INVALID -1
#define NET_REPL_ERR_SHORT -2

/* Schema IDs (bit-pack schema_id). */
#define NET_REPL_SCHEMA_JOIN 0x2001u
#define NET_REPL_SCHEMA_SPAWN 0x2002u
#define NET_REPL_SCHEMA_STATE_CUBE 0x2003u

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_REPLICATION_COMMON_H */

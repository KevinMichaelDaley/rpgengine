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
#define NET_REPL_SCHEMA_WELCOME 0x2004u
#define NET_REPL_SCHEMA_SPAWN_BATCH 0x2005u
#define NET_REPL_SCHEMA_STATE_CUBE_BATCH 0x2006u
#define NET_REPL_SCHEMA_INPUT_ROT 0x2007u
#define NET_REPL_SCHEMA_INPUT_MOVE  0x2008u
#define NET_REPL_SCHEMA_INPUT_SPAWN 0x2009u
#define NET_REPL_SCHEMA_BODY_SPAWN 0x200Au
#define NET_REPL_SCHEMA_BODY_STATE 0x200Bu
#define NET_REPL_SCHEMA_STREAM_FRAME 0x200Cu
#define NET_REPL_SCHEMA_EVENT 0x200Du
#define NET_REPL_SCHEMA_BODY_STATE_BATCH 0x200Eu
#define NET_REPL_SCHEMA_SNAPSHOT_CHUNK 0x200Fu
#define NET_REPL_SCHEMA_MESH_DATA     0x2010u
#define NET_REPL_SCHEMA_STREAM_PRIORITY 0x2011u /* server-assigned asset/chunk stream priority. */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_REPLICATION_COMMON_H */

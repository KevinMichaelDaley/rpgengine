#ifndef FERRUM_NET_TOPIC_DISPATCHER_H
#define FERRUM_NET_TOPIC_DISPATCHER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct job_system;
typedef struct job_system job_system_t;
struct fr_topic_channel;
typedef struct fr_topic_channel fr_topic_channel_t;

/** Configuration is implicit via constructor params to limit public types. */

/** Opaque dispatcher handle. */
typedef struct fr_topic_dispatcher fr_topic_dispatcher_t;

/** Create a dispatcher bound to a job system and topic channels. */
fr_topic_dispatcher_t *fr_topic_dispatcher_create(job_system_t *sys,
                                                  fr_topic_channel_t **topics,
                                                  uint32_t num_topics);

/** Destroy dispatcher and free internal resources. */
void fr_topic_dispatcher_destroy(fr_topic_dispatcher_t *disp);

/** Register a handler for a topic index. Returns 0 on success. */
int fr_topic_dispatcher_register(fr_topic_dispatcher_t *disp,
                                 uint32_t topic_index,
                                 void (*on_message)(const uint8_t *data, size_t len, void *user),
                                 void *user,
                                 int priority,
                                 uint32_t preferred_worker);

/** Start background pump thread. */
int fr_topic_dispatcher_start(fr_topic_dispatcher_t *disp);

/** Stop background pump thread. */
void fr_topic_dispatcher_stop(fr_topic_dispatcher_t *disp);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_NET_TOPIC_DISPATCHER_H */

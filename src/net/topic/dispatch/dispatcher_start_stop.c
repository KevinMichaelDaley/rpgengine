#include <time.h>
#include <string.h>
#include "dispatcher_internal.h"

static void topic_job_trampoline(void *user_data) {
    fr_topic_dispatch_payload_t *p = (fr_topic_dispatch_payload_t*)user_data;
    if (p->entry.on_message) {
        p->entry.on_message(p->data, p->len, p->entry.user);
    }
    (void)apool_free(p->data_pool, p->data_handle);
    (void)apool_free(p->payload_pool, p->payload_handle);
}

static int pump_main(void *arg) {
    fr_topic_dispatcher_t *d = (fr_topic_dispatcher_t*)arg;
    atomic_store(&d->running, true);
    struct timespec ts; ts.tv_sec=0; ts.tv_nsec=1000*1000; /* 1ms */
    while (atomic_load(&d->running)) {
        int any = 0;
        uint8_t buf[FR_TOPIC_DISPATCHER_MAX_MESSAGE_SIZE];
        for (uint32_t i=0;i<d->num_topics;i++) {
            fr_topic_handler_entry_t *h = &d->handlers[i];
            if (!h->on_message) continue; /* no handler registered, skip */
            /* Try to pop one message and dispatch */
            size_t len = sizeof(buf);
            if (fr_topic_channel_pop(d->topics[i], buf, &len)) {
                any = 1;

                apool_handle_t data_handle = apool_alloc(&d->data_pool);
                if (data_handle.index == APOOL_INDEX_INVALID) {
                    continue;
                }
                uint8_t *data = (uint8_t *)apool_get(&d->data_pool, data_handle);
                if (!data) {
                    (void)apool_free(&d->data_pool, data_handle);
                    continue;
                }
                memcpy(data, buf, len);

                apool_handle_t payload_handle = apool_alloc(&d->payload_pool);
                if (payload_handle.index == APOOL_INDEX_INVALID) {
                    (void)apool_free(&d->data_pool, data_handle);
                    continue;
                }
                fr_topic_dispatch_payload_t *p = (fr_topic_dispatch_payload_t *)apool_get(&d->payload_pool, payload_handle);
                if (!p) {
                    (void)apool_free(&d->payload_pool, payload_handle);
                    (void)apool_free(&d->data_pool, data_handle);
                    continue;
                }

                p->entry = *h;
                p->data = data;
                p->len = len;
                p->payload_pool = &d->payload_pool;
                p->payload_handle = payload_handle;
                p->data_pool = &d->data_pool;
                p->data_handle = data_handle;
                if (h->preferred_worker != UINT32_MAX) {
                    job_dispatch_to(d->sys, topic_job_trampoline, p, h->priority, NULL, h->preferred_worker);
                } else {
                    job_dispatch(d->sys, topic_job_trampoline, p, h->priority, NULL);
                }
            }
        }
        if (!any) nanosleep(&ts, NULL);
    }
    return 0;
}

int fr_topic_dispatcher_start(fr_topic_dispatcher_t *disp) {
    if (!disp) return -1;
    thrd_t t;
    if (thrd_create(&t, (thrd_start_t)pump_main, disp) != thrd_success) return -1;
    disp->pump_thread = t;
    return 0;
}

void fr_topic_dispatcher_stop(fr_topic_dispatcher_t *disp) {
    if (!disp) return;
    atomic_store(&disp->running, false);
    thrd_join(disp->pump_thread, NULL);
}

#include <time.h>
#include <stdlib.h>
#include "dispatcher_internal.h"

static void topic_job_trampoline(void *user_data) {
    /* user_data points to a small struct { entry*, data*, len } allocated by pump */
    typedef struct payload {
        fr_topic_handler_entry_t entry;
        uint8_t *data;
        size_t len;
    } payload_t;
    payload_t *p = (payload_t*)user_data;
    if (p->entry.on_message) {
        p->entry.on_message(p->data, p->len, p->entry.user);
    }
    free(p->data);
    free(p);
}

static int pump_main(void *arg) {
    fr_topic_dispatcher_t *d = (fr_topic_dispatcher_t*)arg;
    atomic_store(&d->running, true);
    struct timespec ts; ts.tv_sec=0; ts.tv_nsec=1000*1000; /* 1ms */
    while (atomic_load(&d->running)) {
        int any = 0;
        for (uint32_t i=0;i<d->num_topics;i++) {
            fr_topic_handler_entry_t *h = &d->handlers[i];
            if (!h->on_message) continue; /* no handler registered, skip */
            /* Try to pop one message and dispatch */
            size_t cap = 1024;
            uint8_t *buf = (uint8_t*)malloc(cap);
            if (!buf) continue;
            size_t len = cap;
            if (fr_topic_channel_pop(d->topics[i], buf, &len)) {
                any = 1;
                /* Build payload */
                typedef struct payload {
                    fr_topic_handler_entry_t entry;
                    uint8_t *data;
                    size_t len;
                } payload_t;
                payload_t *p = (payload_t*)malloc(sizeof(payload_t));
                if (!p) { free(buf); continue; }
                p->entry = *h;
                p->data = buf;
                p->len = len;
                if (h->preferred_worker != UINT32_MAX) {
                    job_dispatch_to(d->sys, topic_job_trampoline, p, h->priority, NULL, h->preferred_worker);
                } else {
                    job_dispatch(d->sys, topic_job_trampoline, p, h->priority, NULL);
                }
            } else {
                free(buf);
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

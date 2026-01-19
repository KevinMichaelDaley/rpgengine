#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/net/test_link.h"

static uint64_t add_u64_saturating(uint64_t a, uint64_t b) {
    if (UINT64_MAX - a < b) {
        return UINT64_MAX;
    }
    return a + b;
}

static size_t count_free_slots(const net_test_link_t *link) {
    size_t free_count = 0u;
    for (size_t i = 0u; i < link->slot_count; ++i) {
        if (!link->slot_used[i]) {
            free_count++;
        }
    }
    return free_count;
}

static int select_ready_slot(const net_test_link_t *link, uint64_t now_ns, size_t *out_index) {
    size_t best_index = SIZE_MAX;
    uint64_t best_time = 0u;
    for (size_t i = 0u; i < link->slot_count; ++i) {
        if (!link->slot_used[i]) {
            continue;
        }
        if (link->deliver_times[i] > now_ns) {
            continue;
        }
        if (best_index == SIZE_MAX || link->deliver_times[i] < best_time) {
            best_index = i;
            best_time = link->deliver_times[i];
        }
    }
    if (best_index == SIZE_MAX) {
        return -1;
    }
    *out_index = best_index;
    return 0;
}

int net_test_link_init(net_test_link_t *link,
                       net_test_clock_t *clock,
                       const net_test_step_t *steps,
                       size_t step_count,
                       size_t slot_count,
                       size_t max_payload_size) {
    if (!link || !clock || slot_count == 0u || max_payload_size == 0u) {
        return NET_TEST_LINK_ERR_INVALID;
    }
    if (steps == NULL && step_count > 0u) {
        return NET_TEST_LINK_ERR_INVALID;
    }
    if (max_payload_size > UINT16_MAX) {
        return NET_TEST_LINK_ERR_INVALID;
    }
    if (max_payload_size > SIZE_MAX / slot_count) {
        return NET_TEST_LINK_ERR_INVALID;
    }

    memset(link, 0, sizeof(*link));
    link->clock = clock;
    link->steps = steps;
    link->step_count = step_count;
    link->payload_stride = max_payload_size;
    link->slot_count = slot_count;

    const size_t payload_bytes = slot_count * max_payload_size;
    link->payload_storage = (uint8_t *)calloc(1u, payload_bytes);
    link->payload_sizes = (uint16_t *)calloc(slot_count, sizeof(uint16_t));
    link->deliver_times = (uint64_t *)calloc(slot_count, sizeof(uint64_t));
    link->slot_used = (uint8_t *)calloc(slot_count, sizeof(uint8_t));

    if (!link->payload_storage || !link->payload_sizes || !link->deliver_times || !link->slot_used) {
        net_test_link_destroy(link);
        return NET_TEST_LINK_ERR_OOM;
    }

    return NET_TEST_LINK_OK;
}

void net_test_link_destroy(net_test_link_t *link) {
    if (!link) {
        return;
    }
    free(link->payload_storage);
    free(link->payload_sizes);
    free(link->deliver_times);
    free(link->slot_used);
    memset(link, 0, sizeof(*link));
}

int net_test_link_send(net_test_link_t *link, const void *payload, size_t payload_size) {
    if (!link || !link->clock || !payload) {
        return NET_TEST_LINK_ERR_INVALID;
    }
    if (payload_size > link->payload_stride) {
        return NET_TEST_LINK_ERR_INVALID;
    }

    net_test_step_t step = {1u, 0u, 0u};
    if (link->steps && link->step_index < link->step_count) {
        step = link->steps[link->step_index];
        link->step_index++;
    }

    if (step.copies == 0u) {
        return NET_TEST_LINK_OK;
    }

    if (step.copies > count_free_slots(link)) {
        return NET_TEST_LINK_ERR_FULL;
    }

    const uint64_t now_ns = net_test_clock_now_ns(link->clock);

    for (uint32_t copy_index = 0u; copy_index < step.copies; ++copy_index) {
        size_t slot_index = SIZE_MAX;
        for (size_t i = 0u; i < link->slot_count; ++i) {
            if (!link->slot_used[i]) {
                slot_index = i;
                break;
            }
        }
        if (slot_index == SIZE_MAX) {
            return NET_TEST_LINK_ERR_FULL;
        }
        link->slot_used[slot_index] = 1u;
        link->payload_sizes[slot_index] = (uint16_t)payload_size;
        uint64_t duplicate_offset = step.duplicate_delay_ns * (uint64_t)copy_index;
        uint64_t deliver_time = add_u64_saturating(now_ns, step.delay_ns);
        deliver_time = add_u64_saturating(deliver_time, duplicate_offset);
        link->deliver_times[slot_index] = deliver_time;
        if (payload_size > 0u) {
            memcpy(link->payload_storage + slot_index * link->payload_stride, payload, payload_size);
        }
    }

    return NET_TEST_LINK_OK;
}

int net_test_link_receive(net_test_link_t *link, void *out_payload, size_t out_capacity, size_t *out_size) {
    if (!link || !link->clock || !out_payload || !out_size) {
        return NET_TEST_LINK_ERR_INVALID;
    }

    size_t slot_index = SIZE_MAX;
    uint64_t now_ns = net_test_clock_now_ns(link->clock);
    if (select_ready_slot(link, now_ns, &slot_index) != 0) {
        return NET_TEST_LINK_EMPTY;
    }

    const size_t payload_size = link->payload_sizes[slot_index];
    if (out_capacity < payload_size) {
        return NET_TEST_LINK_ERR_INVALID;
    }

    if (payload_size > 0u) {
        memcpy(out_payload, link->payload_storage + slot_index * link->payload_stride, payload_size);
    }
    *out_size = payload_size;
    link->slot_used[slot_index] = 0u;
    link->payload_sizes[slot_index] = 0u;
    link->deliver_times[slot_index] = 0u;

    return NET_TEST_LINK_OK;
}

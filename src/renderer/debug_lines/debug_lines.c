#include "ferrum/renderer/debug_lines.h"

#include <stddef.h>

void fr_debug_lines_init(fr_debug_lines_t *store, fr_debug_line_t *storage, size_t capacity) {
    if (!store || !storage) {
        return;
    }
    *store = (fr_debug_lines_t){0};
    store->lines = storage;
    store->capacity = capacity;
}

bool fr_debug_lines_add(fr_debug_lines_t *store, vec3_t a, vec3_t b, double now_time_s, double ttl_s) {
    if (!store || !store->lines || store->capacity == 0u) {
        return false;
    }
    if (!(ttl_s > 0.0)) {
        return false;
    }

    size_t idx = 0u;
    if (store->count < store->capacity) {
        idx = (store->head + store->count) % store->capacity;
        store->count += 1u;
    } else {
        idx = store->head;
        store->head = (store->head + 1u) % store->capacity;
    }

    store->lines[idx] = (fr_debug_line_t){
        .a = a,
        .b = b,
        .expire_time_s = now_time_s + ttl_s,
    };
    return true;
}

bool fr_debug_lines_collect_vertices(fr_debug_lines_t *store,
                                   double now_time_s,
                                   vec3_t *out_vertices,
                                   size_t out_vertices_cap,
                                   size_t *out_vertex_count) {
    if (!store || !store->lines || !out_vertices || !out_vertex_count) {
        return false;
    }

    size_t live_count = 0u;

    for (size_t i = 0u; i < store->count; ++i) {
        const size_t idx = (store->head + i) % store->capacity;
        const fr_debug_line_t line = store->lines[idx];
        if (line.expire_time_s <= now_time_s) {
            continue;
        }

        if ((live_count + 1u) * 2u > out_vertices_cap) {
            return false;
        }

        store->lines[live_count] = line;
        out_vertices[live_count * 2u + 0u] = line.a;
        out_vertices[live_count * 2u + 1u] = line.b;
        live_count += 1u;
    }

    store->head = 0u;
    store->count = live_count;
    *out_vertex_count = live_count * 2u;
    return true;
}

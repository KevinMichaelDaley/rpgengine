/**
 * @file json_write.c
 * @brief JSON serializer — writes json_value_t to compact JSON text.
 */

#include "ferrum/editor/json_parse.h"
#include <stdio.h>   /* snprintf */
#include <string.h>
#include <math.h>

/* ----------------------------------------------------------------------- */
/* Internal write helpers                                                    */
/* ----------------------------------------------------------------------- */

typedef struct write_ctx {
    char   *buf;
    size_t  cap;
    size_t  pos;   /* Bytes written (may exceed cap for overflow counting). */
} write_ctx_t;

/** @brief Append a single character. */
static void wc_char_(write_ctx_t *w, char c) {
    if (w->pos < w->cap) w->buf[w->pos] = c;
    w->pos++;
}

/** @brief Append raw bytes. */
static void wc_raw_(write_ctx_t *w, const char *s, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (w->pos < w->cap) w->buf[w->pos] = s[i];
        w->pos++;
    }
}

/** @brief Append a null-terminated string. */
static void wc_str_(write_ctx_t *w, const char *s) {
    wc_raw_(w, s, strlen(s));
}

/** @brief Write a JSON string with proper escaping. */
static void wc_json_string_(write_ctx_t *w, const char *ptr, uint32_t len) {
    wc_char_(w, '"');
    for (uint32_t i = 0; i < len; ++i) {
        char c = ptr[i];
        switch (c) {
            case '"':  wc_raw_(w, "\\\"", 2); break;
            case '\\': wc_raw_(w, "\\\\", 2); break;
            case '\b': wc_raw_(w, "\\b",  2); break;
            case '\f': wc_raw_(w, "\\f",  2); break;
            case '\n': wc_raw_(w, "\\n",  2); break;
            case '\r': wc_raw_(w, "\\r",  2); break;
            case '\t': wc_raw_(w, "\\t",  2); break;
            default:
                if ((unsigned char)c < 0x20) {
                    /* Control character → \u00XX. */
                    char esc[7];
                    snprintf(esc, sizeof(esc), "\\u%04x", (unsigned char)c);
                    wc_raw_(w, esc, 6);
                } else {
                    wc_char_(w, c);
                }
                break;
        }
    }
    wc_char_(w, '"');
}

/* Forward declaration. */
static void wc_value_(write_ctx_t *w, const json_value_t *val);

static void wc_value_(write_ctx_t *w, const json_value_t *val) {
    switch (val->type) {
        case JSON_NULL:
            wc_str_(w, "null");
            break;
        case JSON_BOOL:
            wc_str_(w, val->boolean ? "true" : "false");
            break;
        case JSON_NUMBER: {
            /* Print integers without decimal point. */
            char num_buf[64];
            double v = val->number;
            if (v == (double)(long long)v && fabs(v) < 1e15) {
                snprintf(num_buf, sizeof(num_buf), "%lld", (long long)v);
            } else {
                snprintf(num_buf, sizeof(num_buf), "%.17g", v);
            }
            wc_str_(w, num_buf);
            break;
        }
        case JSON_STRING:
            wc_json_string_(w, val->string.ptr, val->string.len);
            break;
        case JSON_ARRAY:
            wc_char_(w, '[');
            for (uint32_t i = 0; i < val->array.count; ++i) {
                if (i > 0) wc_char_(w, ',');
                wc_value_(w, &val->array.items[i]);
            }
            wc_char_(w, ']');
            break;
        case JSON_OBJECT:
            wc_char_(w, '{');
            for (uint32_t i = 0; i < val->object.count; ++i) {
                if (i > 0) wc_char_(w, ',');
                wc_json_string_(w, val->object.keys[i],
                                val->object.key_lens[i]);
                wc_char_(w, ':');
                wc_value_(w, &val->object.vals[i]);
            }
            wc_char_(w, '}');
            break;
    }
}

/* ----------------------------------------------------------------------- */
/* Public API                                                                */
/* ----------------------------------------------------------------------- */

size_t json_write(const json_value_t *val, char *buf, size_t cap) {
    write_ctx_t w = {.buf = buf, .cap = (cap > 0) ? cap - 1 : 0, .pos = 0};
    if (val) wc_value_(&w, val);
    /* Null-terminate. */
    if (buf && cap > 0) {
        size_t term_pos = (w.pos < cap - 1) ? w.pos : cap - 1;
        buf[term_pos] = '\0';
    }
    return w.pos;
}

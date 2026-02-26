/**
 * @file json_parse.c
 * @brief Minimal JSON parser — arena-allocated, zero-malloc.
 *
 * Recursive-descent parser that reads JSON text and builds a tree of
 * json_value_t nodes in a caller-provided bump arena.
 */

#include "ferrum/editor/json_parse.h"
#include <string.h>
#include <stdlib.h>  /* strtod */

/* ----------------------------------------------------------------------- */
/* Arena helpers                                                             */
/* ----------------------------------------------------------------------- */

void json_arena_init(json_arena_t *arena, void *buf, size_t cap) {
    arena->buf  = (uint8_t *)buf;
    arena->cap  = cap;
    arena->used = 0;
}

/** @brief Allocate n bytes from arena, aligned to 8 bytes. Returns NULL on overflow. */
static void *arena_alloc_(json_arena_t *a, size_t n) {
    /* Align up to 8. */
    size_t aligned = (n + 7u) & ~(size_t)7u;
    if (a->used + aligned > a->cap) return NULL;
    void *ptr = a->buf + a->used;
    a->used += aligned;
    return ptr;
}

/* ----------------------------------------------------------------------- */
/* Parser state                                                              */
/* ----------------------------------------------------------------------- */

typedef struct parse_ctx {
    const char    *src;   /* Input JSON text. */
    size_t         len;   /* Length of input. */
    size_t         pos;   /* Current read position. */
    json_arena_t  *arena;
    bool           error; /* Set on any parse failure. */
} parse_ctx_t;

/* Forward declarations. */
static json_value_t parse_value_(parse_ctx_t *ctx);
static void skip_ws_(parse_ctx_t *ctx);

static char peek_(parse_ctx_t *ctx) {
    if (ctx->pos >= ctx->len) return '\0';
    return ctx->src[ctx->pos];
}

static char advance_(parse_ctx_t *ctx) {
    if (ctx->pos >= ctx->len) { ctx->error = true; return '\0'; }
    return ctx->src[ctx->pos++];
}

static void skip_ws_(parse_ctx_t *ctx) {
    while (ctx->pos < ctx->len) {
        char c = ctx->src[ctx->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            ctx->pos++;
        else
            break;
    }
}

/* ----------------------------------------------------------------------- */
/* Parse primitives                                                          */
/* ----------------------------------------------------------------------- */

static json_value_t parse_null_(parse_ctx_t *ctx) {
    json_value_t v = {.type = JSON_NULL};
    if (ctx->pos + 4 > ctx->len ||
        memcmp(ctx->src + ctx->pos, "null", 4) != 0) {
        ctx->error = true;
        return v;
    }
    ctx->pos += 4;
    return v;
}

static json_value_t parse_true_(parse_ctx_t *ctx) {
    json_value_t v = {.type = JSON_BOOL, .boolean = true};
    if (ctx->pos + 4 > ctx->len ||
        memcmp(ctx->src + ctx->pos, "true", 4) != 0) {
        ctx->error = true;
    } else {
        ctx->pos += 4;
    }
    return v;
}

static json_value_t parse_false_(parse_ctx_t *ctx) {
    json_value_t v = {.type = JSON_BOOL, .boolean = false};
    if (ctx->pos + 5 > ctx->len ||
        memcmp(ctx->src + ctx->pos, "false", 5) != 0) {
        ctx->error = true;
    } else {
        ctx->pos += 5;
    }
    return v;
}

static json_value_t parse_number_(parse_ctx_t *ctx) {
    json_value_t v = {.type = JSON_NUMBER};
    const char *start = ctx->src + ctx->pos;
    char *end = NULL;
    v.number = strtod(start, &end);
    if (end == start) {
        ctx->error = true;
        return v;
    }
    ctx->pos += (size_t)(end - start);
    return v;
}

static json_value_t parse_string_(parse_ctx_t *ctx) {
    json_value_t v = {.type = JSON_STRING};
    v.string.ptr = NULL;
    v.string.len = 0;

    /* Skip opening quote. */
    if (advance_(ctx) != '"') { ctx->error = true; return v; }

    /* First pass: count output length (handling escapes). */
    size_t scan = ctx->pos;
    uint32_t out_len = 0;
    while (scan < ctx->len) {
        char c = ctx->src[scan];
        if (c == '"') break;
        if (c == '\\') {
            scan++; /* skip escape char */
            if (scan >= ctx->len) { ctx->error = true; return v; }
            char esc = ctx->src[scan];
            if (esc == 'u') {
                /* Unicode escape \uXXXX → we'll store as '?' for simplicity. */
                scan += 4;
                if (scan > ctx->len) { ctx->error = true; return v; }
            }
        }
        out_len++;
        scan++;
    }
    if (scan >= ctx->len) { ctx->error = true; return v; }

    /* Allocate output buffer. */
    char *buf = (char *)arena_alloc_(ctx->arena, out_len + 1);
    if (!buf) { ctx->error = true; return v; }

    /* Second pass: decode escapes. */
    uint32_t wi = 0;
    while (ctx->pos < ctx->len) {
        char c = ctx->src[ctx->pos];
        if (c == '"') { ctx->pos++; break; }
        if (c == '\\') {
            ctx->pos++;
            char esc = ctx->src[ctx->pos++];
            switch (esc) {
                case '"':  buf[wi++] = '"';  break;
                case '\\': buf[wi++] = '\\'; break;
                case '/':  buf[wi++] = '/';  break;
                case 'b':  buf[wi++] = '\b'; break;
                case 'f':  buf[wi++] = '\f'; break;
                case 'n':  buf[wi++] = '\n'; break;
                case 'r':  buf[wi++] = '\r'; break;
                case 't':  buf[wi++] = '\t'; break;
                case 'u':  buf[wi++] = '?'; ctx->pos += 4; break;
                default:   buf[wi++] = esc;  break;
            }
        } else {
            buf[wi++] = c;
            ctx->pos++;
        }
    }
    buf[wi] = '\0';

    v.string.ptr = buf;
    v.string.len = out_len;
    return v;
}

/* ----------------------------------------------------------------------- */
/* Parse containers (two-pass: count then fill)                              */
/* ----------------------------------------------------------------------- */

/** @brief Count elements in an array or object without consuming input. */
static uint32_t count_elements_(parse_ctx_t *ctx, char close_char) {
    /* Save position. */
    size_t saved_pos = ctx->pos;
    bool saved_err   = ctx->error;

    uint32_t count = 0;
    int depth = 0;
    skip_ws_(ctx);
    if (peek_(ctx) == close_char) {
        ctx->pos = saved_pos;
        ctx->error = saved_err;
        return 0;
    }

    /* Walk through, counting commas at depth 0. */
    count = 1;
    while (ctx->pos < ctx->len && !ctx->error) {
        char c = ctx->src[ctx->pos];
        if (c == '"') {
            /* Skip string. */
            ctx->pos++;
            while (ctx->pos < ctx->len) {
                char sc = ctx->src[ctx->pos++];
                if (sc == '\\') ctx->pos++; /* skip escaped char */
                else if (sc == '"') break;
            }
            continue;
        }
        if (c == '{' || c == '[') { depth++; ctx->pos++; continue; }
        if (c == '}' || c == ']') {
            if (depth == 0) break;
            depth--;
            ctx->pos++;
            continue;
        }
        if (c == ',' && depth == 0) count++;
        ctx->pos++;
    }

    ctx->pos   = saved_pos;
    ctx->error = saved_err;
    return count;
}

static json_value_t parse_array_(parse_ctx_t *ctx) {
    json_value_t v = {.type = JSON_ARRAY};
    v.array.items = NULL;
    v.array.count = 0;

    /* Skip '['. */
    ctx->pos++;
    skip_ws_(ctx);

    /* Empty array? */
    if (peek_(ctx) == ']') { ctx->pos++; return v; }

    uint32_t count = count_elements_(ctx, ']');
    if (ctx->error) return v;

    json_value_t *items = (json_value_t *)arena_alloc_(
        ctx->arena, count * sizeof(json_value_t));
    if (!items) { ctx->error = true; return v; }

    for (uint32_t i = 0; i < count && !ctx->error; ++i) {
        skip_ws_(ctx);
        items[i] = parse_value_(ctx);
        skip_ws_(ctx);
        if (i + 1 < count) {
            if (peek_(ctx) != ',') { ctx->error = true; return v; }
            ctx->pos++;
        }
    }

    /* Expect ']'. */
    skip_ws_(ctx);
    if (peek_(ctx) != ']') { ctx->error = true; return v; }
    ctx->pos++;

    v.array.items = items;
    v.array.count = count;
    return v;
}

static json_value_t parse_object_(parse_ctx_t *ctx) {
    json_value_t v = {.type = JSON_OBJECT};
    v.object.keys     = NULL;
    v.object.key_lens = NULL;
    v.object.vals     = NULL;
    v.object.count    = 0;

    /* Skip '{'. */
    ctx->pos++;
    skip_ws_(ctx);

    /* Empty object? */
    if (peek_(ctx) == '}') { ctx->pos++; return v; }

    uint32_t count = count_elements_(ctx, '}');
    if (ctx->error) return v;

    const char **keys  = (const char **)arena_alloc_(
        ctx->arena, count * sizeof(const char *));
    uint32_t *key_lens = (uint32_t *)arena_alloc_(
        ctx->arena, count * sizeof(uint32_t));
    json_value_t *vals = (json_value_t *)arena_alloc_(
        ctx->arena, count * sizeof(json_value_t));
    if (!keys || !key_lens || !vals) { ctx->error = true; return v; }

    for (uint32_t i = 0; i < count && !ctx->error; ++i) {
        skip_ws_(ctx);
        /* Parse key (must be a string). */
        json_value_t key_val = parse_string_(ctx);
        if (ctx->error || key_val.type != JSON_STRING) {
            ctx->error = true;
            return v;
        }
        keys[i]     = key_val.string.ptr;
        key_lens[i] = key_val.string.len;

        /* Expect ':'. */
        skip_ws_(ctx);
        if (peek_(ctx) != ':') { ctx->error = true; return v; }
        ctx->pos++;

        /* Parse value. */
        skip_ws_(ctx);
        vals[i] = parse_value_(ctx);

        skip_ws_(ctx);
        if (i + 1 < count) {
            if (peek_(ctx) != ',') { ctx->error = true; return v; }
            ctx->pos++;
        }
    }

    /* Expect '}'. */
    skip_ws_(ctx);
    if (peek_(ctx) != '}') { ctx->error = true; return v; }
    ctx->pos++;

    v.object.keys     = keys;
    v.object.key_lens = key_lens;
    v.object.vals     = vals;
    v.object.count    = count;
    return v;
}

/* ----------------------------------------------------------------------- */
/* Top-level value dispatch                                                  */
/* ----------------------------------------------------------------------- */

static json_value_t parse_value_(parse_ctx_t *ctx) {
    skip_ws_(ctx);
    char c = peek_(ctx);
    switch (c) {
        case 'n': return parse_null_(ctx);
        case 't': return parse_true_(ctx);
        case 'f': return parse_false_(ctx);
        case '"': return parse_string_(ctx);
        case '[': return parse_array_(ctx);
        case '{': return parse_object_(ctx);
        default:
            if (c == '-' || (c >= '0' && c <= '9'))
                return parse_number_(ctx);
            ctx->error = true;
            return (json_value_t){.type = JSON_NULL};
    }
}

/* ----------------------------------------------------------------------- */
/* Public API: parse                                                         */
/* ----------------------------------------------------------------------- */

bool json_parse(const char *input, size_t len, json_arena_t *arena,
                json_value_t *out) {
    if (!input || len == 0 || !arena || !out) {
        if (out) *out = (json_value_t){.type = JSON_NULL};
        return false;
    }

    parse_ctx_t ctx = {
        .src   = input,
        .len   = len,
        .pos   = 0,
        .arena = arena,
        .error = false,
    };

    *out = parse_value_(&ctx);
    if (ctx.error) {
        *out = (json_value_t){.type = JSON_NULL};
        return false;
    }

    /* Verify no trailing non-whitespace. */
    skip_ws_(&ctx);
    if (ctx.pos != ctx.len) {
        *out = (json_value_t){.type = JSON_NULL};
        return false;
    }
    return true;
}

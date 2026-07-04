/**
 * @file procgen_tokenize.c
 * @brief Tokenizer: parses grammar strings into procgen_token_t streams.
 */

#include "ferrum/procgen/procgen_tokenize.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct {
    const char *name;
    tok_type_t  type;
} KEYWORD_TABLE[] = {
    {"ROOM_QUAD",     TOK_ROOM_QUAD},
    {"ROOM_PENT",     TOK_ROOM_PENT},
    {"CORRIDOR_H",    TOK_CORRIDOR_H},
    {"CORRIDOR_V",    TOK_CORRIDOR_V},
    {"CORRIDOR_DIAG", TOK_CORRIDOR_DIAG},
    {"RAMP_UP",       TOK_RAMP_UP},
    {"RAMP_DOWN",     TOK_RAMP_DOWN},
    {"DOOR",          TOK_DOOR},
    {"WINDOW",        TOK_WINDOW},
    {"SPAWN",         TOK_SPAWN},
    {"MARKER",        TOK_MARKER},
    {"BLOCK",         TOK_BLOCK},
    {"EBLOCK",        TOK_EBLOCK},
    {"CONNECT",       TOK_CONNECT},
    {"JUNCTION",      TOK_JUNCTION},
};
#define KW_COUNT (sizeof(KEYWORD_TABLE) / sizeof(KEYWORD_TABLE[0]))

static tok_type_t lookup_keyword(const char *s, uint32_t len) {
    for (uint32_t i = 0; i < KW_COUNT; i++) {
        if (strncmp(s, KEYWORD_TABLE[i].name, len) == 0 &&
            KEYWORD_TABLE[i].name[len] == '\0') {
            return KEYWORD_TABLE[i].type;
        }
    }
    return TOK_ERROR;
}

static int is_ident_start(int c) {
    return isalpha(c) || c == '_';
}

static int is_ident(int c) {
    return isalnum(c) || c == '_';
}

static void emit_err(char *err_buf, uint32_t err_cap,
                     uint32_t line, uint32_t col, const char *msg) {
    snprintf(err_buf, err_cap, "line %u:%u: %s", line, col, msg);
}

tok_error_t procgen_tokenize(const char *input,
                     procgen_token_t *tokens, uint32_t tok_cap,
                     uint32_t *out_count,
                     char *err_buf, uint32_t err_cap) {
    const char *p = input;
    uint32_t line = 1;
    uint32_t col  = 1;
    uint32_t count = 0;
    int block_depth = 0;
    int seen_grammar = 0;

    *out_count = 0;
    if (err_buf && err_cap > 0) err_buf[0] = '\0';

    while (*p) {
        /* Skip whitespace. */
        while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
            if (*p == '\n') { line++; col = 1; }
            else { col++; }
            p++;
        }
        if (*p == '\0') break;

        /* Skip comments. */
        if (*p == '#') {
            while (*p && *p != '\n') { p++; col++; }
            continue;
        }

        /* @grammar header — must come first. */
        if (*p == '@') {
            if (count > 0) {
                emit_err(err_buf, err_cap, line, col,
                         "@grammar must be the first token");
                return TOK_ERR_UNEXPECTED_TOKEN;
            }
            p++; col++;
            if (strncmp(p, "grammar", 7) != 0) {
                emit_err(err_buf, err_cap, line, col,
                         "expected @grammar header");
                return TOK_ERR_UNEXPECTED_TOKEN;
            }
            p += 7; col += 7;

            /* Skip whitespace after @grammar. */
            while (*p == ' ' || *p == '\t') { p++; col++; }

            /* Parse grammar name. */
            const char *name_start = p;
            while (*p && is_ident(*p)) { p++; col++; }
            uint32_t name_len = (uint32_t)(p - name_start);
            if (name_len == 0 || name_len >= 64) {
                emit_err(err_buf, err_cap, line, col,
                         "invalid or missing grammar name");
                return TOK_ERR_MISSING_PARAM;
            }

            /* Skip whitespace before version. */
            while (*p == ' ' || *p == '\t') { p++; col++; }

            /* Expect 'v' prefix. */
            if (*p != 'v' && *p != 'V') {
                emit_err(err_buf, err_cap, line, col,
                         "expected 'v' followed by version number");
                return TOK_ERR_INVALID_NUMBER;
            }
            p++; col++;

            /* Parse version number. */
            if (!isdigit((unsigned char)*p)) {
                emit_err(err_buf, err_cap, line, col,
                         "expected version number after 'v'");
                return TOK_ERR_INVALID_NUMBER;
            }
            uint32_t version = 0;
            while (*p && isdigit((unsigned char)*p)) {
                version = version * 10 + (uint32_t)(*p - '0');
                p++; col++;
            }

            if (count >= tok_cap) {
                emit_err(err_buf, err_cap, 0, 0, "token buffer overflow");
                return TOK_ERR_BUFFER_FULL;
            }
            procgen_token_t *tok = &tokens[count++];
            memset(tok, 0, sizeof(*tok));
            tok->type = TOK_GRAMMAR;
            tok->line = line;
            tok->col  = col;
            tok->grammar_version = version;
            memcpy(tok->value.s, name_start, name_len);
            tok->value.s[name_len] = '\0';
            seen_grammar = 1;
            continue;
        }

        if (!seen_grammar) {
            emit_err(err_buf, err_cap, line, col,
                     "input must start with @grammar header");
            return TOK_ERR_UNEXPECTED_TOKEN;
        }

        /* Keyword or identifier. */
        if (is_ident_start((unsigned char)*p)) {
            const char *kw_start = p;
            while (*p && is_ident((unsigned char)*p)) { p++; col++; }
            uint32_t kw_len = (uint32_t)(p - kw_start);

            tok_type_t kw = lookup_keyword(kw_start, kw_len);
            if (kw == TOK_ERROR) {
                char msg[128];
                snprintf(msg, sizeof(msg), "unknown keyword '%.*s'",
                         (int)kw_len, kw_start);
                emit_err(err_buf, err_cap, line, col - kw_len, msg);
                return TOK_ERR_UNEXPECTED_TOKEN;
            }

            if (count >= tok_cap) {
                emit_err(err_buf, err_cap, 0, 0, "token buffer overflow");
                return TOK_ERR_BUFFER_FULL;
            }
            procgen_token_t *tok = &tokens[count++];
            memset(tok, 0, sizeof(*tok));
            tok->type = kw;
            tok->line = line;
            tok->col  = (uint32_t)(col - kw_len);

            if (kw == TOK_BLOCK) {
                block_depth++;
            } else if (kw == TOK_EBLOCK) {
                if (block_depth == 0) {
                    emit_err(err_buf, err_cap, line, col - 6,
                             "EBLOCK without matching BLOCK");
                    return TOK_ERR_UNBALANCED_BLOCK;
                }
                block_depth--;
            }

            /* Skip whitespace. */
            while (*p == ' ' || *p == '\t') { p++; col++; }

            /* Parse parameters (NAME=value pairs). */
            while (*p && is_ident_start((unsigned char)*p)) {
                const char *param_name = p;
                while (*p && is_ident((unsigned char)*p)) { p++; col++; }
                uint32_t pn_len = (uint32_t)(p - param_name);

                /* Skip whitespace before '='. */
                while (*p == ' ' || *p == '\t') { p++; col++; }

                if (*p != '=') {
                    char msg[128];
                    snprintf(msg, sizeof(msg),
                             "expected '=' after parameter '%.*s'",
                             (int)pn_len, param_name);
                    emit_err(err_buf, err_cap, line, col, msg);
                    return TOK_ERR_MISSING_PARAM;
                }
                p++; col++;

                /* Skip whitespace after '='. */
                while (*p == ' ' || *p == '\t') { p++; col++; }

                /* Parse parameter value. */
                if (*p == '\0') {
                    emit_err(err_buf, err_cap, line, col,
                             "unexpected end of input after '='");
                    return TOK_ERR_MISSING_PARAM;
                }

                /* Parse the value — consumed tokens go inline. */
                if (*p == '"') {
                    /* Quoted string. */
                    p++; col++;
                    const char *str_start = p;
                    while (*p && *p != '"' && *p != '\n') { p++; col++; }
                    if (*p != '"') {
                        emit_err(err_buf, err_cap, line, col,
                                 "unterminated string literal");
                        return TOK_ERR_UNEXPECTED_TOKEN;
                    }
                    uint32_t slen = (uint32_t)(p - str_start);
                    p++; col++; /* skip closing quote */

                    if (count >= tok_cap) {
                        emit_err(err_buf, err_cap, 0, 0, "token buffer overflow");
                        return TOK_ERR_BUFFER_FULL;
                    }
                    procgen_token_t *pt = &tokens[count++];
                    memset(pt, 0, sizeof(*pt));
                    pt->type = TOK_MARKER;  /* reuse for parameter */
                    pt->line = line;
                    pt->col  = col;
                    memcpy(pt->param_name, param_name,
                           pn_len < 31 ? pn_len : 31);
                    pt->param_name[pn_len < 31 ? pn_len : 31] = '\0';
                    memcpy(pt->value.s, str_start,
                           slen < 63 ? slen : 63);
                    pt->value.s[slen < 63 ? slen : 63] = '\0';
                    /* Store param name in... hmm, we don't have space.
                       For now, just emit the value token. The
                       rasterizer knows the keyword's parameter order. */
                } else if (*p == '(') {
                    /* Coordinate tuple: (x,y), (x,y,z), or ((x1,y1),...)
                       Capture as a raw string for the rasterizer to parse. */
                    const char *tup_start = p;
                    p++; col++;
                    int depth = 1;
                    while (*p && depth > 0) {
                        if (*p == '(') depth++;
                        else if (*p == ')') depth--;
                        if (*p == '\n') { line++; col = 1; }
                        else { col++; }
                        if (depth > 0) p++;
                    }
                    if (depth > 0) {
                        emit_err(err_buf, err_cap, line, col,
                                 "unterminated coordinate tuple");
                        return TOK_ERR_UNEXPECTED_TOKEN;
                    }
                    p++; col++; /* skip closing ) */
                    uint32_t tup_len = (uint32_t)(p - tup_start);

                    if (count >= tok_cap) {
                        emit_err(err_buf, err_cap, 0, 0, "token buffer overflow");
                        return TOK_ERR_BUFFER_FULL;
                    }
                    procgen_token_t *pt = &tokens[count++];
                    memset(pt, 0, sizeof(*pt));
                    pt->type = TOK_MARKER;
                    pt->line = line;
                    pt->col  = (uint32_t)(col - tup_len);
                    memcpy(pt->param_name, param_name,
                           pn_len < 31 ? pn_len : 31);
                    pt->param_name[pn_len < 31 ? pn_len : 31] = '\0';
                    /* Store the raw tuple string (with parens) in value.s. */
                    uint32_t copy = tup_len < 63 ? tup_len : 63;
                    memcpy(pt->value.s, tup_start, copy);
                    pt->value.s[copy] = '\0';
                } else if (*p == '-' || isdigit((unsigned char)*p)) {
                    /* Number: int or float. */
                    const char *num_start = p;
                    if (*p == '-') { p++; col++; }
                    while (*p && (isdigit((unsigned char)*p) || *p == '.')) {
                        p++; col++;
                    }
                    uint32_t num_len = (uint32_t)(p - num_start);

                    if (count >= tok_cap) {
                        emit_err(err_buf, err_cap, 0, 0, "token buffer overflow");
                        return TOK_ERR_BUFFER_FULL;
                    }
                    procgen_token_t *pt = &tokens[count++];
                    memset(pt, 0, sizeof(*pt));
                    pt->type = TOK_MARKER;
                    pt->line = line;
                    pt->col  = col - num_len;

                    memcpy(pt->param_name, param_name,
                           pn_len < 31 ? pn_len : 31);
                    pt->param_name[pn_len < 31 ? pn_len : 31] = '\0';

                    char num_buf[64];
                    uint32_t ncopy = num_len < 63 ? num_len : 63;
                    memcpy(num_buf, num_start, ncopy);
                    num_buf[ncopy] = '\0';
                    pt->value.f = (float)atof(num_buf);
                }
                /* else it's an unquoted string identifier */
                else if (is_ident_start((unsigned char)*p)) {
                    const char *str_start = p;
                    while (*p && is_ident((unsigned char)*p)) { p++; col++; }
                    uint32_t slen = (uint32_t)(p - str_start);

                    if (count >= tok_cap) {
                        emit_err(err_buf, err_cap, 0, 0, "token buffer overflow");
                        return TOK_ERR_BUFFER_FULL;
                    }
                    procgen_token_t *pt = &tokens[count++];
                    memset(pt, 0, sizeof(*pt));
                    pt->type = TOK_MARKER;
                    pt->line = line;
                    pt->col  = col - slen;
                    memcpy(pt->param_name, param_name,
                           pn_len < 31 ? pn_len : 31);
                    pt->param_name[pn_len < 31 ? pn_len : 31] = '\0';
                    memcpy(pt->value.s, str_start,
                           slen < 63 ? slen : 63);
                    pt->value.s[slen < 63 ? slen : 63] = '\0';
                }

                /* Skip whitespace before next param or keyword. */
                while (*p == ' ' || *p == '\t') { p++; col++; }
            }
            continue;
        }

        /* Unexpected character. */
        {
            char msg[64];
            snprintf(msg, sizeof(msg),
                     "unexpected character '%c' (0x%02x)",
                     isprint((unsigned char)*p) ? *p : '?',
                     (unsigned char)*p);
            emit_err(err_buf, err_cap, line, col, msg);
            return TOK_ERR_UNEXPECTED_TOKEN;
        }
    }

    /* Check block nesting balance. */
    if (block_depth > 0) {
        emit_err(err_buf, err_cap, 0, 0,
                 "unclosed BLOCK at end of input");
        return TOK_ERR_UNBALANCED_BLOCK;
    }

    if (!seen_grammar) {
        emit_err(err_buf, err_cap, 0, 0,
                 "empty input or missing @grammar header");
        return TOK_ERR_UNEXPECTED_TOKEN;
    }

    *out_count = count;
    return 0;
}

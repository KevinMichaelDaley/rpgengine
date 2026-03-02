/**
 * @file aegis_asm_compile.c
 * @brief Aegis IL assembler: main compilation and label resolution.
 *
 * Two-pass assembly:
 *   Pass 1: Parse lines, emit instructions with placeholder labels, record fixups.
 *   Pass 2: Resolve all label fixups.
 *
 * 4 non-static functions: aegis_asm_compile + 3 static helpers below limit.
 * (Only aegis_asm_compile is non-static.)
 */

#include "ferrum/aegis/aegis_asm.h"
#include "ferrum/aegis/aegis_event.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declaration for operand parsing (in aegis_asm_parse.c). */
extern bool aegis_asm_parse_operand(const char *token, uint32_t *out_val,
                                    bool *is_imm, bool *is_label,
                                    char *label_buf);

/* ----------------------------------------------------------------------- */
/* Opcode mnemonic table                                                    */
/* ----------------------------------------------------------------------- */

typedef struct {
    const char    *name;
    aegis_opcode_t opcode;
} mnemonic_entry_t;

static const mnemonic_entry_t g_mnemonics[] = {
    {"yield",         AEGIS_OP_YIELD},
    {"resume",        AEGIS_OP_RESUME},
    {"exit",          AEGIS_OP_EXIT},
    {"call",          AEGIS_OP_CALL},
    {"ret",           AEGIS_OP_RET},
    {"wait",          AEGIS_OP_WAIT},
    {"poll",          AEGIS_OP_POLL},
    {"event_type",    AEGIS_OP_EVENT_TYPE},
    {"event_src",     AEGIS_OP_EVENT_SRC},
    {"event_field",   AEGIS_OP_EVENT_FIELD},
    {"query_entity",  AEGIS_OP_QUERY_ENTITY},
    {"get_attr",      AEGIS_OP_GET_ATTR},
    {"entity_count",  AEGIS_OP_ENTITY_COUNT},
    {"entity_at",     AEGIS_OP_ENTITY_AT},
    {"vis_test",      AEGIS_OP_VIS_TEST},
    {"nav_query",     AEGIS_OP_NAV_QUERY},
    {"build_update",  AEGIS_OP_BUILD_UPDATE},
    {"target_entity", AEGIS_OP_TARGET_ENTITY},
    {"set_field",     AEGIS_OP_SET_FIELD},
    {"add_hint",      AEGIS_OP_ADD_HINT},
    {"push_update",   AEGIS_OP_PUSH_UPDATE},
    {"mov",           AEGIS_OP_MOV},
    {"load_imm",      AEGIS_OP_LOAD_IMM},
    {"load_imm64",    AEGIS_OP_LOAD_IMM64},
    {"add",           AEGIS_OP_ADD},
    {"sub",           AEGIS_OP_SUB},
    {"mul",           AEGIS_OP_MUL},
    {"div",           AEGIS_OP_DIV},
    {"mod",           AEGIS_OP_MOD},
    {"neg",           AEGIS_OP_NEG},
    {"and",           AEGIS_OP_AND},
    {"or",            AEGIS_OP_OR},
    {"xor",           AEGIS_OP_XOR},
    {"not",           AEGIS_OP_NOT},
    {"eq",            AEGIS_OP_EQ},
    {"ne",            AEGIS_OP_NE},
    {"lt",            AEGIS_OP_LT},
    {"le",            AEGIS_OP_LE},
    {"gt",            AEGIS_OP_GT},
    {"ge",            AEGIS_OP_GE},
    {"jmp",           AEGIS_OP_JMP},
    {"jmp_if",        AEGIS_OP_JMP_IF},
    {"jmp_if_not",    AEGIS_OP_JMP_IF_NOT},
    {"alloc",         AEGIS_OP_ALLOC},
    {"load",          AEGIS_OP_LOAD},
    {"store",         AEGIS_OP_STORE},
    {"static_load",   AEGIS_OP_STATIC_LOAD},
    {"static_store",  AEGIS_OP_STATIC_STORE},
    {"push",          AEGIS_OP_PUSH},
    {"pop",           AEGIS_OP_POP},
    {"i32_to_f32",    AEGIS_OP_I32_TO_F32},
    {"f32_to_i32",    AEGIS_OP_F32_TO_I32},
    {"i64_to_f64",    AEGIS_OP_I64_TO_F64},
    {"f64_to_i64",    AEGIS_OP_F64_TO_I64},
    {"f64_to_f32",    AEGIS_OP_F64_TO_F32},
    {"f32_to_f64",    AEGIS_OP_F32_TO_F64},
    {"vec3_add",      AEGIS_OP_VEC3_ADD},
    {"vec3_sub",      AEGIS_OP_VEC3_SUB},
    {"vec3_mul",      AEGIS_OP_VEC3_MUL},
    {"vec3_scale",    AEGIS_OP_VEC3_SCALE},
    {"vec3_dot",      AEGIS_OP_VEC3_DOT},
    {"vec3_cross",    AEGIS_OP_VEC3_CROSS},
    {"vec3_len",      AEGIS_OP_VEC3_LEN},
    {"vec3_norm",     AEGIS_OP_VEC3_NORM},
    {"quat_mul",      AEGIS_OP_QUAT_MUL},
    {"quat_rotate",   AEGIS_OP_QUAT_ROTATE},
    {"signal",        AEGIS_OP_SIGNAL},
    {"subscribe",     AEGIS_OP_SUBSCRIBE},
    {"await_event",   AEGIS_OP_AWAIT_EVENT},
    {NULL, 0}
};

/* ----------------------------------------------------------------------- */
/* Internal helpers                                                         */
/* ----------------------------------------------------------------------- */

/** Set assembler error state. */
static void asm_error(aegis_asm_t *as, uint32_t line, const char *fmt, ...) {
    as->has_error  = true;
    as->error_line = line;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(as->error_msg, AEGIS_ASM_MAX_ERROR, fmt, ap);
    va_end(ap);
}

/** Look up opcode by mnemonic name. Returns true if found. */
static bool lookup_mnemonic(const char *name, aegis_opcode_t *out) {
    for (const mnemonic_entry_t *e = g_mnemonics; e->name; e++) {
        if (strcmp(e->name, name) == 0) {
            *out = e->opcode;
            return true;
        }
    }
    return false;
}

/** Register or create a label. Returns label index. */
static int find_or_create_label(aegis_asm_t *as, const char *name) {
    /* Search existing. */
    for (uint32_t i = 0; i < as->label_count; i++) {
        if (strcmp(as->labels[i].name, name) == 0) {
            return (int)i;
        }
    }
    /* Create new. */
    if (as->label_count >= AEGIS_ASM_MAX_LABELS) {
        return -1;
    }
    int idx = (int)as->label_count;
    strncpy(as->labels[idx].name, name, 63);
    as->labels[idx].name[63] = '\0';
    as->labels[idx].defined  = false;
    as->labels[idx].pc       = 0;
    as->label_count++;
    return idx;
}

/** Emit an instruction, growing the buffer if needed. */
static bool emit_insn(aegis_asm_t *as, aegis_instruction_t insn) {
    if (as->insn_count >= as->insn_cap) {
        uint32_t new_cap = as->insn_cap == 0 ? 64 : as->insn_cap * 2;
        aegis_instruction_t *new_buf = (aegis_instruction_t *)realloc(
            as->insns, new_cap * sizeof(aegis_instruction_t));
        if (!new_buf) return false;
        as->insns    = new_buf;
        as->insn_cap = new_cap;
    }
    as->insns[as->insn_count++] = insn;
    return true;
}

/** Add a fixup for a label reference. */
static bool add_fixup(aegis_asm_t *as, const char *label,
                      uint32_t insn_idx, uint32_t operand, uint32_t line) {
    if (as->fixup_count >= AEGIS_ASM_MAX_FIXUPS) {
        return false;
    }
    aegis_asm_fixup_t *f = &as->fixups[as->fixup_count++];
    size_t len = strlen(label);
    if (len > 63) len = 63;
    memcpy(f->label, label, len);
    f->label[len] = '\0';
    f->insn_idx  = insn_idx;
    f->operand   = operand;
    f->line      = line;
    return true;
}

/** Tokenize a line: split on commas and whitespace into tokens.
 *  Returns number of tokens. Modifies line_buf in place. */
static int tokenize_line(char *line_buf, char *tokens[], int max_tokens) {
    int count = 0;
    char *p = line_buf;

    while (*p && count < max_tokens) {
        /* Skip whitespace and commas. */
        while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (!*p) break;

        tokens[count++] = p;
        /* Advance to next delimiter. */
        while (*p && !isspace((unsigned char)*p) && *p != ',') p++;
        if (*p) {
            *p = '\0';
            p++;
        }
    }
    return count;
}

/** Handle a directive line (.topic, .static, .fuel). */
static bool handle_directive(aegis_asm_t *as, const char *directive,
                             char *tokens[], int token_count, uint32_t line) {
    if (strcmp(directive, ".static") == 0) {
        if (token_count < 2) {
            asm_error(as, line, ".static requires a size argument");
            return false;
        }
        as->static_size = (uint32_t)strtoul(tokens[1], NULL, 10);
        return true;
    }
    if (strcmp(directive, ".fuel") == 0) {
        if (token_count < 2) {
            asm_error(as, line, ".fuel requires a value argument");
            return false;
        }
        as->fuel_budget = (uint32_t)strtoul(tokens[1], NULL, 10);
        return true;
    }
    if (strcmp(directive, ".topic") == 0) {
        if (token_count < 2) {
            asm_error(as, line, ".topic requires a topic name");
            return false;
        }
        /* Strip optional quotes. */
        char *name = tokens[1];
        size_t len = strlen(name);
        if (len >= 2 && name[0] == '"' && name[len - 1] == '"') {
            name[len - 1] = '\0';
            name++;
        }
        as->topic_hash = aegis_topic_hash(name);
        return true;
    }
    asm_error(as, line, "unknown directive: %s", directive);
    return false;
}

/** Process a single line of IL source. */
static bool process_line(aegis_asm_t *as, char *line_buf, uint32_t line_num) {
    /* Strip comments (;  or //) from line. */
    char *comment = strstr(line_buf, "//");
    if (comment) *comment = '\0';
    comment = strchr(line_buf, ';');
    if (comment) *comment = '\0';

    /* Tokenize. */
    char *tokens[8];
    int ntokens = tokenize_line(line_buf, tokens, 8);
    if (ntokens == 0) return true; /* Empty / comment-only line. */

    /* Check for directive. */
    if (tokens[0][0] == '.') {
        return handle_directive(as, tokens[0], tokens, ntokens, line_num);
    }

    /* Check for label definition (ends with ':'). */
    size_t first_len = strlen(tokens[0]);
    if (first_len > 1 && tokens[0][first_len - 1] == ':') {
        tokens[0][first_len - 1] = '\0'; /* Remove colon. */
        int idx = find_or_create_label(as, tokens[0]);
        if (idx < 0) {
            asm_error(as, line_num, "too many labels");
            return false;
        }
        if (as->labels[idx].defined) {
            asm_error(as, line_num, "duplicate label: %s", tokens[0]);
            return false;
        }
        as->labels[idx].defined = true;
        as->labels[idx].pc      = as->insn_count;
        return true;
    }

    /* Must be an instruction. Look up mnemonic. */
    aegis_opcode_t opcode;
    if (!lookup_mnemonic(tokens[0], &opcode)) {
        asm_error(as, line_num, "unknown mnemonic: %s", tokens[0]);
        return false;
    }

    /* Parse up to 3 operands. */
    uint32_t op_vals[3] = {0, 0, 0};
    uint32_t imm_flags  = 0;

    for (int i = 1; i < ntokens && i <= 3; i++) {
        uint32_t val = 0;
        bool     is_imm = false;
        bool     is_label = false;
        char     label_buf[64] = {0};

        if (!aegis_asm_parse_operand(tokens[i], &val, &is_imm, &is_label,
                                     label_buf)) {
            asm_error(as, line_num, "invalid operand: %s", tokens[i]);
            return false;
        }

        if (is_label) {
            /* Label reference: add fixup, use 0 as placeholder. */
            is_imm = true; /* Labels resolve to immediate PC values. */
            if (!add_fixup(as, label_buf, as->insn_count,
                           (uint32_t)(i - 1), line_num)) {
                asm_error(as, line_num, "too many label fixups");
                return false;
            }
            val = 0; /* Placeholder. */
        }

        if (is_imm) {
            /* Set immediate flag for this operand position. */
            if (i == 1)      imm_flags |= AEGIS_IMM_A;
            else if (i == 2) imm_flags |= AEGIS_IMM_B;
            else if (i == 3) imm_flags |= AEGIS_IMM_C;
        } else {
            /* Validate register index. */
            if (val > 255) {
                asm_error(as, line_num, "register index out of range: r%u",
                          val);
                return false;
            }
        }

        op_vals[i - 1] = val;
    }

    /* Emit instruction. */
    aegis_instruction_t insn = aegis_insn_make(
        opcode, imm_flags, op_vals[0], op_vals[1], op_vals[2]);
    if (!emit_insn(as, insn)) {
        asm_error(as, line_num, "out of memory");
        return false;
    }

    return true;
}

/* ----------------------------------------------------------------------- */
/* Compile (public)                                                         */
/* ----------------------------------------------------------------------- */

bool aegis_asm_compile(aegis_asm_t *as, const char *source,
                       uint32_t source_len, aegis_bytecode_t *out_bc) {
    /* Reset state but preserve the struct. */
    if (as->insns) {
        free(as->insns);
    }
    aegis_asm_init(as);
    aegis_bytecode_init(out_bc);

    /* Process source line by line. */
    uint32_t line_num = 0;
    const char *p   = source;
    const char *end = source + source_len;

    while (p < end) {
        line_num++;

        /* Find end of line. */
        const char *eol = p;
        while (eol < end && *eol != '\n') eol++;

        /* Copy line to mutable buffer. */
        uint32_t line_len = (uint32_t)(eol - p);
        char line_buf[1024];
        if (line_len >= sizeof(line_buf)) line_len = sizeof(line_buf) - 1;
        memcpy(line_buf, p, line_len);
        line_buf[line_len] = '\0';

        if (!process_line(as, line_buf, line_num)) {
            return false;
        }

        p = (eol < end) ? eol + 1 : end;
    }

    /* Pass 2: Resolve label fixups. */
    for (uint32_t i = 0; i < as->fixup_count; i++) {
        aegis_asm_fixup_t *f = &as->fixups[i];

        /* Find the label. */
        bool found = false;
        for (uint32_t j = 0; j < as->label_count; j++) {
            if (strcmp(as->labels[j].name, f->label) == 0) {
                if (!as->labels[j].defined) {
                    asm_error(as, f->line, "undefined label: %s", f->label);
                    return false;
                }
                /* Patch the operand in the instruction. */
                as->insns[f->insn_idx].words[f->operand + 1] =
                    as->labels[j].pc;
                found = true;
                break;
            }
        }
        if (!found) {
            asm_error(as, f->line, "undefined label: %s", f->label);
            return false;
        }
    }

    /* Build output bytecode. */
    out_bc->instruction_count = as->insn_count;
    out_bc->static_size       = as->static_size;
    out_bc->topic_hash        = as->topic_hash;

    if (as->insn_count > 0) {
        /* Transfer ownership of instruction buffer to bytecode. */
        out_bc->instructions = as->insns;
        as->insns    = NULL;
        as->insn_cap = 0;
    } else {
        out_bc->instructions = NULL;
        free(as->insns);
        as->insns    = NULL;
        as->insn_cap = 0;
    }

    return true;
}

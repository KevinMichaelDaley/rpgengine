/**
 * @file aegis_asm_parse.c
 * @brief Aegis IL assembler: tokenizer and line parser.
 *
 * Parses one line at a time from IL source text. Each line is either:
 *   - A directive (.topic, .static, .fuel)
 *   - A label definition (name:)
 *   - An instruction (mnemonic operands...)
 *   - Empty / comment-only (skipped)
 *
 * 4 non-static functions: aegis_asm_init, aegis_asm_error,
 * aegis_asm_error_line, aegis_asm_parse_operand.
 */

#include "ferrum/aegis/aegis_asm.h"
#include "ferrum/aegis/aegis_event.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ----------------------------------------------------------------------- */
/* Init                                                                     */
/* ----------------------------------------------------------------------- */

void aegis_asm_init(aegis_asm_t *as) {
    memset(as, 0, sizeof(*as));
}

/* ----------------------------------------------------------------------- */
/* Error accessors                                                          */
/* ----------------------------------------------------------------------- */

const char *aegis_asm_error(const aegis_asm_t *as) {
    return as->has_error ? as->error_msg : "";
}

uint32_t aegis_asm_error_line(const aegis_asm_t *as) {
    return as->has_error ? as->error_line : 0;
}

/* ----------------------------------------------------------------------- */
/* Operand parsing (exported for use by aegis_asm_compile.c)                */
/* ----------------------------------------------------------------------- */

/**
 * @brief Parse a single operand token.
 *
 * Recognizes:
 *   r0..r255  → register (returns reg index, *is_imm = false)
 *   123       → decimal immediate (*is_imm = true)
 *   -42       → negative decimal
 *   0xFF      → hex immediate
 *   label     → label reference (*is_label = true, name copied to label_buf)
 *
 * @param token     Null-terminated token string.
 * @param out_val   Output: register index or immediate value.
 * @param is_imm    Output: true if the operand is an immediate.
 * @param is_label  Output: true if the operand is a label name.
 * @param label_buf Output: label name (if is_label), must be >= 64 bytes.
 * @return true on success, false on parse error.
 */
bool aegis_asm_parse_operand(const char *token, uint32_t *out_val,
                             bool *is_imm, bool *is_label,
                             char *label_buf) {
    *is_imm   = false;
    *is_label = false;

    if (!token || !token[0]) {
        return false;
    }

    /* Register: r0..r255 */
    if (token[0] == 'r' && isdigit((unsigned char)token[1])) {
        long val = strtol(token + 1, NULL, 10);
        if (val < 0 || val > 255) {
            return false;
        }
        *out_val = (uint32_t)val;
        *is_imm  = false;
        return true;
    }

    /* Hex immediate: 0x... */
    if (token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) {
        unsigned long val = strtoul(token, NULL, 16);
        *out_val = (uint32_t)val;
        *is_imm  = true;
        return true;
    }

    /* Decimal immediate: starts with digit or minus-sign followed by digit. */
    if (isdigit((unsigned char)token[0]) ||
        (token[0] == '-' && isdigit((unsigned char)token[1]))) {
        long val = strtol(token, NULL, 10);
        uint32_t uval;
        memcpy(&uval, &val, sizeof(uint32_t));
        *out_val = uval;
        *is_imm  = true;
        return true;
    }

    /* Label reference: alphabetic identifier. */
    if (isalpha((unsigned char)token[0]) || token[0] == '_') {
        size_t len = strlen(token);
        if (len >= 64) len = 63;
        memcpy(label_buf, token, len);
        label_buf[len] = '\0';
        *is_label = true;
        return true;
    }

    return false;
}

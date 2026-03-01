/**
 * @file aegis_ops_arith.h
 * @brief Aegis arithmetic, bitwise, comparison, and type conversion ops.
 *
 * Per ref/aegis_bytecode_spec.md §3.3.
 *
 * All handlers are pure functions: read source registers, write destination.
 * Arithmetic operates on i32 fields (wrapping on overflow).
 * Div/mod by zero return false without modifying dst.
 *
 * Ownership: callers own all register pointers.
 * Nullability: all pointers must be non-NULL.
 */

#ifndef FERRUM_AEGIS_OPS_ARITH_H
#define FERRUM_AEGIS_OPS_ARITH_H

#include <stdbool.h>
#include "ferrum/aegis/aegis_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -- Arithmetic (i32, wrapping) -- */

/** dst = a + b (wrapping). Always returns true. */
bool aegis_op_add(aegis_register_t *dst,
                  const aegis_register_t *a, const aegis_register_t *b);

/** dst = a - b (wrapping). Always returns true. */
bool aegis_op_sub(aegis_register_t *dst,
                  const aegis_register_t *a, const aegis_register_t *b);

/** dst = a * b (wrapping). Always returns true. */
bool aegis_op_mul(aegis_register_t *dst,
                  const aegis_register_t *a, const aegis_register_t *b);

/** dst = a / b. Returns false on division by zero. */
bool aegis_op_div(aegis_register_t *dst,
                  const aegis_register_t *a, const aegis_register_t *b);

/** dst = a % b. Returns false on division by zero. */
bool aegis_op_mod(aegis_register_t *dst,
                  const aegis_register_t *a, const aegis_register_t *b);

/** dst = -a. Always returns true. */
bool aegis_op_neg(aegis_register_t *dst, const aegis_register_t *a);

/* -- Bitwise (u32) -- */

/** dst = a & b. */
void aegis_op_and(aegis_register_t *dst,
                  const aegis_register_t *a, const aegis_register_t *b);

/** dst = a | b. */
void aegis_op_or(aegis_register_t *dst,
                 const aegis_register_t *a, const aegis_register_t *b);

/** dst = a ^ b. */
void aegis_op_xor(aegis_register_t *dst,
                  const aegis_register_t *a, const aegis_register_t *b);

/** dst = ~a. */
void aegis_op_not(aegis_register_t *dst, const aegis_register_t *a);

/* -- Comparison (result is bool: u32 0 or 1) -- */

void aegis_op_eq(aegis_register_t *dst,
                 const aegis_register_t *a, const aegis_register_t *b);
void aegis_op_ne(aegis_register_t *dst,
                 const aegis_register_t *a, const aegis_register_t *b);
void aegis_op_lt(aegis_register_t *dst,
                 const aegis_register_t *a, const aegis_register_t *b);
void aegis_op_le(aegis_register_t *dst,
                 const aegis_register_t *a, const aegis_register_t *b);
void aegis_op_gt(aegis_register_t *dst,
                 const aegis_register_t *a, const aegis_register_t *b);
void aegis_op_ge(aegis_register_t *dst,
                 const aegis_register_t *a, const aegis_register_t *b);

/* -- Type conversion -- */

void aegis_op_i32_to_f32(aegis_register_t *dst, const aegis_register_t *src);
void aegis_op_f32_to_i32(aegis_register_t *dst, const aegis_register_t *src);
void aegis_op_i64_to_f64(aegis_register_t *dst, const aegis_register_t *src);
void aegis_op_f64_to_i64(aegis_register_t *dst, const aegis_register_t *src);
void aegis_op_f64_to_f32(aegis_register_t *dst, const aegis_register_t *src);
void aegis_op_f32_to_f64(aegis_register_t *dst, const aegis_register_t *src);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_AEGIS_OPS_ARITH_H */

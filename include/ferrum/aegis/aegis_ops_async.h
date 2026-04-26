/**
 * @file aegis_ops_async.h
 * @brief Async bytecode instruction handlers: vis_test, nav_query, poll, wait.
 *
 * These handlers implement the async world query pipeline:
 * - vis_test / nav_query: allocate a heap result slot, build a task
 *   descriptor, submit to the MPSC async buffer, return a handle.
 * - poll: non-blocking status check; copies result on COMPLETE.
 * - wait: polls, returns false if PENDING (caller should wait-yield).
 *
 * Ownership: handlers borrow the VM and decoded instruction. The VM
 *   owns the async_tasks[] tracking array and references the async_buffer.
 * Nullability: vm and d must not be NULL. vm->async_buffer must be set.
 * Error semantics: return false on error (buffer full, limit exceeded,
 *   invalid handle). For wait, false means "should wait-yield" (PENDING).
 */
#ifndef FERRUM_AEGIS_OPS_ASYNC_H
#define FERRUM_AEGIS_OPS_ASYNC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

struct aegis_vm;
struct aegis_decode_result;

/**
 * @brief Submit an async raycast (vis_test r_handle, r_origin, r_ray_vec).
 *
 * Allocates a 16-byte result slot in the heap arena, builds a task with
 * AEGIS_TASK_VIS_TEST type, copies origin + ray_vec into params, submits
 * to the async buffer, and stores the handle (heap offset) in r_handle.
 *
 * @return true on success, false if limit exceeded or buffer full.
 */
bool aegis_op_vis_test(struct aegis_vm *vm, const struct aegis_decode_result *d);

/**
 * @brief Submit an async nav mesh query (nav_query r_handle, r_from, r_to).
 *
 * Same pattern as vis_test but with AEGIS_TASK_NAV_QUERY type and
 * from/to point parameters.
 *
 * @return true on success, false if limit exceeded or buffer full.
 */
bool aegis_op_nav_query(struct aegis_vm *vm, const struct aegis_decode_result *d);

/**
 * @brief Non-blocking poll of async task status (poll r_result, r_flag, r_handle).
 *
 * Reads the atomic status of the task identified by handle. If COMPLETE,
 * copies 16 bytes from the result slot into r_result. Sets r_flag to
 * the status value (PENDING, COMPLETE, or ERROR).
 *
 * @return true on success, false if handle is invalid.
 */
bool aegis_op_poll(struct aegis_vm *vm, const struct aegis_decode_result *d);

/**
 * @brief Blocking wait on async task (wait r_result, r_flag, r_handle).
 *
 * Executes a poll. If status is PENDING, returns false (the interpreter
 * should wait-yield without advancing PC). If COMPLETE or ERROR, behaves
 * like poll and returns true (advance PC normally).
 *
 * @return true if task is done (advance PC), false if PENDING (wait-yield).
 */
bool aegis_op_wait(struct aegis_vm *vm, const struct aegis_decode_result *d);

/**
 * @brief Submit an async sense query (sense_query r_handle, r_mode_flags, r_target).
 *
 * Allocates a 4 KB result slot in the heap arena, builds a task with
 * AEGIS_TASK_SENSE_QUERY type, packs query_mode + sense_flags + target_entity
 * + npc_position + max_range into params, submits to the async buffer.
 *
 * @return true on success, false if limit exceeded or buffer full.
 */
bool aegis_op_sense_query(struct aegis_vm *vm, const struct aegis_decode_result *d);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_AEGIS_OPS_ASYNC_H */

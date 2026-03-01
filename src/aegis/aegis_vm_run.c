/**
 * @file aegis_vm_run.c
 * @brief Aegis VM main interpreter loop.
 *
 * Per ref/aegis_bytecode_spec.md §3, §6, §8.
 *
 * Single dispatch function using a switch statement over all opcodes.
 * Phase 2/3 opcodes (events, async, entity queries) return errors
 * until their implementing tickets land.
 */

#include "ferrum/aegis/aegis_vm.h"
#include "ferrum/aegis/aegis_decode.h"
#include "ferrum/aegis/aegis_ops_arith.h"
#include "ferrum/aegis/aegis_ops_data.h"
#include "ferrum/aegis/aegis_ops_event.h"
#include "ferrum/aegis/aegis_ops_entity.h"
#include "ferrum/aegis/aegis_ops_update.h"
#include "ferrum/aegis/aegis_ops_flow.h"
#include "ferrum/aegis/aegis_ops_math.h"

/** Helper: set error status and return. */
static aegis_vm_status_t vm_error(aegis_vm_t *vm, uint32_t code) {
    vm->status    = AEGIS_VM_ERROR;
    vm->exit_code = code;
    return AEGIS_VM_ERROR;
}

aegis_vm_status_t aegis_vm_run(aegis_vm_t *vm) {
    const uint32_t insn_count = vm->bytecode->instruction_count;

    while (vm->alive) {
        /* Fuel check: force-yield if exhausted. */
        if (!aegis_vm_consume_fuel(vm)) {
            aegis_vm_force_yield(vm);
            return AEGIS_VM_FORCE_YIELDED;
        }

        /* PC bounds check. */
        if (vm->pc >= insn_count) {
            return vm_error(vm, 0xFFFF);
        }

        /* Fetch and decode. */
        const aegis_instruction_t *insn = &vm->bytecode->instructions[vm->pc];
        aegis_decode_result_t d;
        if (!aegis_decode(insn, vm->regs, &d)) {
            return vm_error(vm, 0xFFFE);
        }

        bool pc_modified = false;

        switch (d.opcode) {

        /* ---- Coroutine control ---- */

        case AEGIS_OP_RESUME:
            /* Entry marker — no-op in Phase 1. */
            break;

        case AEGIS_OP_YIELD:
            vm->pc++; /* advance past yield for next resume */
            return aegis_vm_yield(vm);

        case AEGIS_OP_EXIT:
            aegis_vm_exit(vm, d.a.u32);
            return AEGIS_VM_EXITED;

        /* ---- Function calls ---- */

        case AEGIS_OP_CALL:
            if (!aegis_op_call(&vm->memory, vm->pc, d.raw_a,
                               insn_count, &vm->pc)) {
                return vm_error(vm, 0xFFFD);
            }
            pc_modified = true;
            break;

        case AEGIS_OP_RET:
            if (!aegis_op_ret(&vm->memory, &vm->pc)) {
                return vm_error(vm, 0xFFFC);
            }
            pc_modified = true;
            break;

        /* ---- Data movement ---- */

        case AEGIS_OP_MOV:
            aegis_op_mov(&vm->regs[d.raw_a], &d.b);
            break;

        case AEGIS_OP_LOAD_IMM:
            aegis_op_load_imm(&vm->regs[d.raw_a], d.raw_b);
            break;

        case AEGIS_OP_LOAD_IMM64:
            aegis_op_load_imm64(&vm->regs[d.raw_a], d.raw_b, d.raw_c);
            break;

        /* ---- Arithmetic ---- */

        case AEGIS_OP_ADD:
            aegis_op_add(&vm->regs[d.raw_a], &d.b, &d.c);
            break;

        case AEGIS_OP_SUB:
            aegis_op_sub(&vm->regs[d.raw_a], &d.b, &d.c);
            break;

        case AEGIS_OP_MUL:
            aegis_op_mul(&vm->regs[d.raw_a], &d.b, &d.c);
            break;

        case AEGIS_OP_DIV:
            if (!aegis_op_div(&vm->regs[d.raw_a], &d.b, &d.c)) {
                return vm_error(vm, 0xFFF0);
            }
            break;

        case AEGIS_OP_MOD:
            if (!aegis_op_mod(&vm->regs[d.raw_a], &d.b, &d.c)) {
                return vm_error(vm, 0xFFF0);
            }
            break;

        case AEGIS_OP_NEG:
            aegis_op_neg(&vm->regs[d.raw_a], &d.b);
            break;

        /* ---- Bitwise ---- */

        case AEGIS_OP_AND:
            aegis_op_and(&vm->regs[d.raw_a], &d.b, &d.c);
            break;

        case AEGIS_OP_OR:
            aegis_op_or(&vm->regs[d.raw_a], &d.b, &d.c);
            break;

        case AEGIS_OP_XOR:
            aegis_op_xor(&vm->regs[d.raw_a], &d.b, &d.c);
            break;

        case AEGIS_OP_NOT:
            aegis_op_not(&vm->regs[d.raw_a], &d.b);
            break;

        /* ---- Comparison ---- */

        case AEGIS_OP_EQ:
            aegis_op_eq(&vm->regs[d.raw_a], &d.b, &d.c);
            break;

        case AEGIS_OP_NE:
            aegis_op_ne(&vm->regs[d.raw_a], &d.b, &d.c);
            break;

        case AEGIS_OP_LT:
            aegis_op_lt(&vm->regs[d.raw_a], &d.b, &d.c);
            break;

        case AEGIS_OP_LE:
            aegis_op_le(&vm->regs[d.raw_a], &d.b, &d.c);
            break;

        case AEGIS_OP_GT:
            aegis_op_gt(&vm->regs[d.raw_a], &d.b, &d.c);
            break;

        case AEGIS_OP_GE:
            aegis_op_ge(&vm->regs[d.raw_a], &d.b, &d.c);
            break;

        /* ---- Control flow ---- */

        case AEGIS_OP_JMP:
            if (!aegis_op_jmp(d.raw_a, insn_count, &vm->pc)) {
                return vm_error(vm, 0xFFFB);
            }
            pc_modified = true;
            break;

        case AEGIS_OP_JMP_IF:
            if (!aegis_op_jmp_if(&d.a, d.raw_b, vm->pc, insn_count,
                                 &vm->pc)) {
                return vm_error(vm, 0xFFFB);
            }
            pc_modified = true;
            break;

        case AEGIS_OP_JMP_IF_NOT:
            if (!aegis_op_jmp_if_not(&d.a, d.raw_b, vm->pc, insn_count,
                                     &vm->pc)) {
                return vm_error(vm, 0xFFFB);
            }
            pc_modified = true;
            break;

        /* ---- Memory ---- */

        case AEGIS_OP_ALLOC: {
            int32_t off = aegis_memory_alloc(&vm->memory, d.b.u32);
            if (off < 0) {
                return vm_error(vm, 0xFFEF);
            }
            vm->regs[d.raw_a].i32 = off;
            break;
        }

        case AEGIS_OP_LOAD:
            if (!aegis_memory_heap_load(&vm->memory, d.b.u32, d.c.u32,
                                        &vm->regs[d.raw_a])) {
                return vm_error(vm, 0xFFEE);
            }
            break;

        case AEGIS_OP_STORE:
            if (!aegis_memory_heap_store(&vm->memory, d.b.u32, d.c.u32,
                                         &d.a)) {
                return vm_error(vm, 0xFFEE);
            }
            break;

        case AEGIS_OP_STATIC_LOAD:
            if (!aegis_memory_static_load(&vm->memory, d.b.u32,
                                          &vm->regs[d.raw_a])) {
                return vm_error(vm, 0xFFED);
            }
            break;

        case AEGIS_OP_STATIC_STORE:
            if (!aegis_memory_static_store(&vm->memory, d.raw_a, &d.b)) {
                return vm_error(vm, 0xFFED);
            }
            break;

        case AEGIS_OP_PUSH:
            if (!aegis_memory_stack_push(&vm->memory, &d.a)) {
                return vm_error(vm, 0xFFEC);
            }
            break;

        case AEGIS_OP_POP:
            if (!aegis_memory_stack_pop(&vm->memory, &vm->regs[d.raw_a])) {
                return vm_error(vm, 0xFFEC);
            }
            break;

        /* ---- Type conversion ---- */

        case AEGIS_OP_I32_TO_F32:
            aegis_op_i32_to_f32(&vm->regs[d.raw_a], &d.b);
            break;

        case AEGIS_OP_F32_TO_I32:
            aegis_op_f32_to_i32(&vm->regs[d.raw_a], &d.b);
            break;

        case AEGIS_OP_I64_TO_F64:
            aegis_op_i64_to_f64(&vm->regs[d.raw_a], &d.b);
            break;

        case AEGIS_OP_F64_TO_I64:
            aegis_op_f64_to_i64(&vm->regs[d.raw_a], &d.b);
            break;

        case AEGIS_OP_F64_TO_F32:
            aegis_op_f64_to_f32(&vm->regs[d.raw_a], &d.b);
            break;

        case AEGIS_OP_F32_TO_F64:
            aegis_op_f32_to_f64(&vm->regs[d.raw_a], &d.b);
            break;

        /* ---- Vector & quaternion math ---- */

        case AEGIS_OP_VEC3_ADD:
            aegis_op_vec3_add(&vm->regs[d.raw_a], &d.b, &d.c);
            break;

        case AEGIS_OP_VEC3_SUB:
            aegis_op_vec3_sub(&vm->regs[d.raw_a], &d.b, &d.c);
            break;

        case AEGIS_OP_VEC3_MUL:
            aegis_op_vec3_mul(&vm->regs[d.raw_a], &d.b, &d.c);
            break;

        case AEGIS_OP_VEC3_SCALE:
            aegis_op_vec3_scale(&vm->regs[d.raw_a], &d.b, &d.c);
            break;

        case AEGIS_OP_VEC3_DOT:
            aegis_op_vec3_dot(&vm->regs[d.raw_a], &d.b, &d.c);
            break;

        case AEGIS_OP_VEC3_CROSS:
            aegis_op_vec3_cross(&vm->regs[d.raw_a], &d.b, &d.c);
            break;

        case AEGIS_OP_VEC3_LEN:
            aegis_op_vec3_len(&vm->regs[d.raw_a], &d.b);
            break;

        case AEGIS_OP_VEC3_NORM:
            aegis_op_vec3_norm(&vm->regs[d.raw_a], &d.b);
            break;

        case AEGIS_OP_QUAT_MUL:
            aegis_op_quat_mul(&vm->regs[d.raw_a], &d.b, &d.c);
            break;

        case AEGIS_OP_QUAT_ROTATE:
            aegis_op_quat_rotate(&vm->regs[d.raw_a], &d.b, &d.c);
            break;

        /* ---- Event access (Phase 2) ---- */

        case AEGIS_OP_EVENT_TYPE:
            if (!aegis_op_event_type(&vm->regs[d.raw_a], vm->event)) {
                return vm_error(vm, 0xFFE0);
            }
            break;

        case AEGIS_OP_EVENT_SRC:
            if (!aegis_op_event_src(&vm->regs[d.raw_a], vm->event)) {
                return vm_error(vm, 0xFFE0);
            }
            break;

        case AEGIS_OP_EVENT_FIELD:
            /* A=dst reg, B=byte offset (imm or reg), C=size (imm or reg) */
            if (!aegis_op_event_field(&vm->regs[d.raw_a], vm->event,
                                      d.b.u32, d.c.u32)) {
                return vm_error(vm, 0xFFDF);
            }
            break;

        /* ---- Entity queries (Phase 2) ---- */

        case AEGIS_OP_QUERY_ENTITY:
            if (!aegis_op_query_entity(&vm->regs[d.raw_a], &d.b,
                                        vm->entity_view)) {
                return vm_error(vm, 0xFFD0);
            }
            break;

        case AEGIS_OP_GET_ATTR:
            /* A=dst reg, B=handle (reg or imm), C=key (imm). */
            if (!aegis_op_get_attr(&vm->regs[d.raw_a], d.b.u32,
                                    (uint16_t)d.c.u32, vm->entity_view)) {
                return vm_error(vm, 0xFFCF);
            }
            break;

        case AEGIS_OP_ENTITY_COUNT:
            if (!aegis_op_entity_count(&vm->regs[d.raw_a],
                                        vm->entity_view)) {
                return vm_error(vm, 0xFFCE);
            }
            break;

        case AEGIS_OP_ENTITY_AT:
            if (!aegis_op_entity_at(&vm->regs[d.raw_a], &d.b,
                                     vm->entity_view)) {
                return vm_error(vm, 0xFFCD);
            }
            break;

        /* ---- Update construction (Phase 2) ---- */

        case AEGIS_OP_BUILD_UPDATE:
            aegis_op_build_update(&vm->regs[d.raw_a], &vm->staging);
            break;

        case AEGIS_OP_TARGET_ENTITY:
            aegis_op_target_entity(&vm->staging, &d.b);
            break;

        case AEGIS_OP_SET_FIELD:
            /* A=builder (ignored), B=key (imm), C=value (reg). */
            if (!aegis_op_set_field(&vm->staging, (uint16_t)d.b.u32,
                                     &d.c)) {
                return vm_error(vm, 0xFFC0);
            }
            break;

        case AEGIS_OP_ADD_HINT:
            aegis_op_add_hint(&vm->staging, d.b.u32);
            break;

        case AEGIS_OP_PUSH_UPDATE:
            if (!vm->update_set ||
                !aegis_op_push_update(vm->update_set, &vm->staging)) {
                return vm_error(vm, 0xFFBF);
            }
            break;

        /* ---- Phase 2/3 stubs (not yet implemented) ---- */

        case AEGIS_OP_WAIT:
        case AEGIS_OP_POLL:
        case AEGIS_OP_VIS_TEST:
        case AEGIS_OP_NAV_QUERY:
            return vm_error(vm, 0xFF00 | (uint32_t)d.opcode);

        default:
            return vm_error(vm, 0xFE00 | (uint32_t)d.opcode);
        }

        if (!pc_modified) {
            vm->pc++;
        }
    }

    return vm->status;
}

/*
 * STM8 TCG translation front-end.
 *
 * STM8 instructions are variable-length (1–5 bytes).
 * Prefix bytes 0x72, 0x90, 0x91, 0x92 extend the instruction set.
 *
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */

#include <string.h>
#include "cpu.h"
#include "arch_callbacks.h"
#include "tb-helper.h"
#include "debug.h"

/* ------------------------------------------------------------------ */
/* TCG globals                                                         */
/* ------------------------------------------------------------------ */

static TCGv cpu_a;
static TCGv cpu_x;
static TCGv cpu_y;
static TCGv cpu_sp;
static TCGv cpu_pc;
static TCGv cpu_cc;

void gen_sync_pc(DisasContext *dc)
{
    tcg_gen_movi_tl(cpu_pc, dc->base.pc);
}

void setup_disas_context(DisasContextBase *dc, CPUState *env)
{
    dc->mem_idx = cpu_mmu_index(env);
}

void translate_init(void)
{
    cpu_a  = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, a),  "A");
    cpu_x  = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, x),  "X");
    cpu_y  = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, y),  "Y");
    cpu_sp = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, sp), "SP");
    cpu_pc = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, pc), "PC");
    cpu_cc = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, cc), "CC");
}

static inline void gen_goto_tb(TranslationBlock *tb, int n,
                                target_ulong dest, target_ulong pc_unused)
{
    if((tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK)) {
        tcg_gen_goto_tb(n);
        tcg_gen_movi_tl(cpu_pc, dest);
        gen_exit_tb(tb, n);
    } else {
        tcg_gen_movi_tl(cpu_pc, dest);
        gen_exit_tb_no_chaining(tb);
    }
}

/* ------------------------------------------------------------------ */
/* Fetch helpers                                                       */
/* ------------------------------------------------------------------ */

static inline uint8_t fetch8(target_ulong pc)
{
    return (uint8_t)ldub_code(pc);
}

static inline uint16_t fetch16(target_ulong pc)
{
    return ((uint16_t)ldub_code(pc) << 8) | ldub_code(pc + 1); /* big-endian */
}

static inline uint32_t fetch24(target_ulong pc)
{
    return ((uint32_t)ldub_code(pc) << 16) |
           ((uint32_t)ldub_code(pc + 1) << 8) |
            (uint32_t)ldub_code(pc + 2);
}

/* ------------------------------------------------------------------ */
/* Helpers: push/pop A bypass SP                                      */
/* ------------------------------------------------------------------ */

static void gen_push8(TCGv val)
{
    TCGv addr = tcg_temp_new();
    tcg_gen_mov_tl(addr, cpu_sp);
    tcg_gen_qemu_st8(val, addr, 0);
    tcg_gen_subi_tl(cpu_sp, cpu_sp, 1);
    tcg_gen_andi_tl(cpu_sp, cpu_sp, 0xFFFF);
    tcg_temp_free(addr);
}

static void gen_pop8(TCGv dst)
{
    TCGv addr = tcg_temp_new();
    tcg_gen_addi_tl(cpu_sp, cpu_sp, 1);
    tcg_gen_andi_tl(cpu_sp, cpu_sp, 0xFFFF);
    tcg_gen_mov_tl(addr, cpu_sp);
    tcg_gen_qemu_ld8u(dst, addr, 0);
    tcg_temp_free(addr);
}

/* ------------------------------------------------------------------ */
/* Instruction decoder                                                 */
/* ------------------------------------------------------------------ */

static int disas_stm8_insn(CPUState *env, DisasContext *dc)
{
    target_ulong pc   = dc->base.pc;
    uint8_t      op   = fetch8(pc);
    int          size = 1;  /* will be updated */

    switch(op)
    {
    /* NOP (0x9D) */
    case 0x9D:
        size = 1;
        break;

    /* LD A,#byte (0xA6 nn) */
    case 0xA6: {
        uint8_t imm = fetch8(pc + 1);
        tcg_gen_movi_tl(cpu_a, imm);
        size = 2;
        break;
    }

    /* LD A,shortoff (0xB6 nn) — short direct data space */
    case 0xB6: {
        uint8_t addr8 = fetch8(pc + 1);
        TCGv addr = tcg_const_tl((target_ulong)addr8);
        tcg_gen_qemu_ld8u(cpu_a, addr, 0);
        tcg_temp_free(addr);
        size = 2;
        break;
    }

    /* LD A,(X) (0xF6) */
    case 0xF6:
        tcg_gen_qemu_ld8u(cpu_a, cpu_x, 0);
        size = 1;
        break;

    /* LD (X),A (0xF7) */
    case 0xF7:
        tcg_gen_qemu_st8(cpu_a, cpu_x, 0);
        size = 1;
        break;

    /* LD shortmem,A (0xB7 nn) */
    case 0xB7: {
        uint8_t addr8 = fetch8(pc + 1);
        TCGv addr = tcg_const_tl((target_ulong)addr8);
        tcg_gen_qemu_st8(cpu_a, addr, 0);
        tcg_temp_free(addr);
        size = 2;
        break;
    }

    /* LD A,longmem (0xC6 nn mm) */
    case 0xC6: {
        uint16_t addr16 = fetch16(pc + 1);
        TCGv addr = tcg_const_tl((target_ulong)addr16);
        tcg_gen_qemu_ld8u(cpu_a, addr, 0);
        tcg_temp_free(addr);
        size = 3;
        break;
    }

    /* LD longmem,A (0xC7 nn mm) */
    case 0xC7: {
        uint16_t addr16 = fetch16(pc + 1);
        TCGv addr = tcg_const_tl((target_ulong)addr16);
        tcg_gen_qemu_st8(cpu_a, addr, 0);
        tcg_temp_free(addr);
        size = 3;
        break;
    }

    /* CLRA (0x4F) */
    case 0x4F:
        tcg_gen_movi_tl(cpu_a, 0);
        size = 1;
        break;

    /* INC A (0x4C) */
    case 0x4C:
        tcg_gen_addi_tl(cpu_a, cpu_a, 1);
        tcg_gen_andi_tl(cpu_a, cpu_a, 0xFF);
        size = 1;
        break;

    /* DEC A (0x4A) */
    case 0x4A:
        tcg_gen_subi_tl(cpu_a, cpu_a, 1);
        tcg_gen_andi_tl(cpu_a, cpu_a, 0xFF);
        size = 1;
        break;

    /* ADD A,#byte (0xAB nn) */
    case 0xAB: {
        uint8_t imm = fetch8(pc + 1);
        tcg_gen_addi_tl(cpu_a, cpu_a, imm);
        tcg_gen_andi_tl(cpu_a, cpu_a, 0xFF);
        size = 2;
        break;
    }

    /* SUB A,#byte (0xA0 nn) */
    case 0xA0: {
        uint8_t imm = fetch8(pc + 1);
        tcg_gen_subi_tl(cpu_a, cpu_a, imm);
        tcg_gen_andi_tl(cpu_a, cpu_a, 0xFF);
        size = 2;
        break;
    }

    /* AND A,#byte (0xA4 nn) */
    case 0xA4: {
        uint8_t imm = fetch8(pc + 1);
        tcg_gen_andi_tl(cpu_a, cpu_a, imm);
        size = 2;
        break;
    }

    /* OR A,#byte (0xAA nn) */
    case 0xAA: {
        uint8_t imm = fetch8(pc + 1);
        TCGv v = tcg_const_tl(imm);
        tcg_gen_or_tl(cpu_a, cpu_a, v);
        tcg_temp_free(v);
        size = 2;
        break;
    }

    /* XOR A,#byte (0xA8 nn) */
    case 0xA8: {
        uint8_t imm = fetch8(pc + 1);
        tcg_gen_xori_tl(cpu_a, cpu_a, imm);
        size = 2;
        break;
    }

    /* LDW X,#nn (0xAE nn mm) — 16-bit immediate into X */
    case 0xAE: {
        uint16_t imm = fetch16(pc + 1);
        tcg_gen_movi_tl(cpu_x, imm);
        size = 3;
        break;
    }

    /* LDW Y,#nn (0x90 0xAE nn mm — prefix 0x90) handled below at prefix */

    /* ADDW X,#nn (0x1C nn mm) */
    case 0x1C: {
        uint16_t imm = fetch16(pc + 1);
        tcg_gen_addi_tl(cpu_x, cpu_x, imm);
        tcg_gen_andi_tl(cpu_x, cpu_x, 0xFFFF);
        size = 3;
        break;
    }

    /* SUBW X,#nn (0x1D nn mm) */
    case 0x1D: {
        uint16_t imm = fetch16(pc + 1);
        tcg_gen_subi_tl(cpu_x, cpu_x, imm);
        tcg_gen_andi_tl(cpu_x, cpu_x, 0xFFFF);
        size = 3;
        break;
    }

    /* PUSH A (0x88) */
    case 0x88:
        gen_push8(cpu_a);
        size = 1;
        break;

    /* POP A (0x84) */
    case 0x84:
        gen_pop8(cpu_a);
        size = 1;
        break;

    /* PUSH CC (0x86) */
    case 0x86:
        gen_push8(cpu_cc);
        size = 1;
        break;

    /* POP CC (0x85) */
    case 0x85:
        gen_pop8(cpu_cc);
        size = 1;
        break;

    /* CALL longmem (0xCD nn mm) */
    case 0xCD: {
        uint16_t target = fetch16(pc + 1);
        target_ulong ret = pc + 3;
        TCGv ret_lo = tcg_const_tl(ret & 0xFF);
        TCGv ret_hi = tcg_const_tl((ret >> 8) & 0xFF);
        gen_push8(ret_lo);
        gen_push8(ret_hi);
        tcg_temp_free(ret_lo);
        tcg_temp_free(ret_hi);
        tcg_gen_movi_tl(cpu_pc, target);
        dc->base.is_jmp = DISAS_JUMP;
        size = 3;
        break;
    }

    /* RET (0x81) */
    case 0x81: {
        TCGv hi = tcg_temp_new();
        TCGv lo = tcg_temp_new();
        gen_pop8(hi);
        gen_pop8(lo);
        TCGv ret_pc = tcg_temp_new();
        tcg_gen_shli_tl(ret_pc, hi, 8);
        tcg_gen_or_tl(ret_pc, ret_pc, lo);
        tcg_gen_mov_tl(cpu_pc, ret_pc);
        tcg_temp_free(hi); tcg_temp_free(lo); tcg_temp_free(ret_pc);
        dc->base.is_jmp = DISAS_JUMP;
        size = 1;
        break;
    }

    /* IRET (0x80) */
    case 0x80: {
        /* Pop CC, A, XH, XL, YH, YL, PCE, PCH, PCL */
        gen_pop8(cpu_cc);
        gen_pop8(cpu_a);
        TCGv xh = tcg_temp_new();
        TCGv xl = tcg_temp_new();
        TCGv yh = tcg_temp_new();
        TCGv yl = tcg_temp_new();
        TCGv pcH = tcg_temp_new();
        TCGv pcM = tcg_temp_new();
        TCGv pcL = tcg_temp_new();
        gen_pop8(xh); gen_pop8(xl);
        gen_pop8(yh); gen_pop8(yl);
        gen_pop8(pcH); gen_pop8(pcM); gen_pop8(pcL);
        TCGv xval = tcg_temp_new();
        tcg_gen_shli_tl(xval, xh, 8); tcg_gen_or_tl(cpu_x, xval, xl);
        TCGv yval = tcg_temp_new();
        tcg_gen_shli_tl(yval, yh, 8); tcg_gen_or_tl(cpu_y, yval, yl);
        TCGv pcval = tcg_temp_new();
        tcg_gen_shli_tl(pcval, pcH, 16);
        TCGv tmp = tcg_temp_new();
        tcg_gen_shli_tl(tmp, pcM, 8); tcg_gen_or_tl(pcval, pcval, tmp);
        tcg_gen_or_tl(cpu_pc, pcval, pcL);
        tcg_temp_free(xh); tcg_temp_free(xl); tcg_temp_free(yh); tcg_temp_free(yl);
        tcg_temp_free(pcH); tcg_temp_free(pcM); tcg_temp_free(pcL);
        tcg_temp_free(xval); tcg_temp_free(yval); tcg_temp_free(pcval); tcg_temp_free(tmp);
        dc->base.is_jmp = DISAS_JUMP;
        size = 1;
        break;
    }

    /* TRAP (0x83) */
    case 0x83:
        gen_helper_raise_exception(cpu_env, tcg_const_i32(EXCP_TRAP));
        dc->base.is_jmp = DISAS_JUMP;
        size = 1;
        break;

    /* HALT (0x8E) */
    case 0x8E:
        gen_helper_halt(cpu_env);
        dc->base.is_jmp = DISAS_JUMP;
        size = 1;
        break;

    /* WFI (0x8F) */
    case 0x8F:
        gen_helper_wfi(cpu_env);
        dc->base.is_jmp = DISAS_JUMP;
        size = 1;
        break;

    /* JP longmem (0xCC nn mm) — unconditional absolute jump */
    case 0xCC: {
        uint16_t target = fetch16(pc + 1);
        tcg_gen_movi_tl(cpu_pc, target);
        dc->base.is_jmp = DISAS_JUMP;
        size = 3;
        break;
    }

    /* JRA offset (0x20 nn) — unconditional relative */
    case 0x20: {
        int8_t offset = (int8_t)fetch8(pc + 1);
        target_ulong target = (pc + 2) + (target_ulong)(int64_t)offset;
        tcg_gen_movi_tl(cpu_pc, target);
        dc->base.is_jmp = DISAS_JUMP;
        size = 2;
        break;
    }

    /* Conditional branches 0x25-0x2F (JRC, JRNC, JRZ, JRNZ…) */
    case 0x25: case 0x26: case 0x27: case 0x28:
    case 0x29: case 0x2A: case 0x2B: case 0x2C:
    case 0x2D: case 0x2E: case 0x2F: {
        int8_t offset = (int8_t)fetch8(pc + 1);
        target_ulong taken    = (pc + 2) + (target_ulong)(int64_t)offset;
        target_ulong not_taken = pc + 2;
        /* Simplified: just emit an unconditional jump to taken address */
        /* A full implementation would check CC bits */
        (void)not_taken;
        tcg_gen_movi_tl(cpu_pc, taken);
        dc->base.is_jmp = DISAS_JUMP;
        size = 2;
        break;
    }

    /* Prefix 0x90 — Y-space instructions */
    case 0x90: {
        uint8_t op2 = fetch8(pc + 1);
        if(op2 == 0xAE) {
            /* LDW Y,#nn */
            uint16_t imm = fetch16(pc + 2);
            tcg_gen_movi_tl(cpu_y, imm);
            size = 4;
        } else {
            goto unimplemented;
        }
        break;
    }

    default:
    unimplemented:
        tlib_printf(LOG_LEVEL_DEBUG,
                    "STM8: Unimplemented opcode 0x%02X at PC=0x%06X\n",
                    op, (unsigned)pc);
        gen_helper_raise_exception(cpu_env, tcg_const_i32(EXCP_UNDEF));
        dc->base.is_jmp = DISAS_JUMP;
        size = 1;
        break;
    }

    dc->base.pc += size;
    return size;
}

/* ------------------------------------------------------------------ */
/* Boilerplate                                                         */
/* ------------------------------------------------------------------ */

int gen_breakpoint(DisasContextBase *base, CPUBreakpoint *bp)
{
    (void)bp;
    gen_helper_raise_exception(cpu_env, tcg_const_i32(EXCP_UNDEF));
    gen_exit_tb_no_chaining(base->tb);
    base->is_jmp = DISAS_JUMP;
    return 1;
}

int gen_intermediate_code(CPUState *env, DisasContextBase *base)
{
    DisasContext *dc = (DisasContext *)base;
    tcg_gen_insn_start(base->pc);
    base->tb->size += disas_stm8_insn(env, dc);
    if((base->pc - base->tb->pc) != base->tb->size) return 0;
    if((base->pc & (TARGET_PAGE_SIZE - 1)) == 0)    return 0;
    return 1;
}

uint32_t gen_intermediate_code_epilogue(CPUState *env, DisasContextBase *base)
{
    DisasContext *dc = (DisasContext *)base;
    if(!dc->base.is_jmp) gen_goto_tb(dc->base.tb, 0, dc->base.pc, dc->base.pc);
    return 0;
}

void restore_state_to_opc(CPUState *env, TranslationBlock *tb, target_ulong *data)
{
    env->pc = data[0];
}

int process_interrupt(int interrupt_request, CPUState *env)
{
    if(tlib_is_in_debug_mode()) return 0;
    if(interrupt_request & CPU_INTERRUPT_HARD) {
        if(cpu_interrupts_enabled(env)) {
            int32_t vec = tlib_find_best_interrupt();
            if(vec > 0) {
                env->exception_index = EXCP_INT_FIRST + vec - 1;
                do_interrupt(env);
                return 1;
            }
        }
    } else if(interrupt_request & CPU_INTERRUPT_RESET) {
        cpu_reset(env);
    }
    return 0;
}

void cpu_exec_prologue(CPUState *env) { (void)env; }
void cpu_exec_epilogue(CPUState *env) { (void)env; }

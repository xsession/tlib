/*
 * TMS320C28x (C2000) TCG translation front-end.
 *
 * The C28x core has 16-bit instruction words; some instructions span
 * two consecutive 16-bit words (32-bit instructions).
 * Byte addressing: one 16-bit word = 2 bytes.  PC is always even.
 *
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "cpu.h"
#include "arch_callbacks.h"
#include "tb-helper.h"
#include "debug.h"

/* ------------------------------------------------------------------ */
/* TCG global register handles                                         */
/* ------------------------------------------------------------------ */

static TCGv cpu_acc;
static TCGv cpu_p;
static TCGv cpu_xt;
static TCGv cpu_xar[8];
static TCGv cpu_sp;
static TCGv cpu_pc;
static TCGv cpu_rpc;
static TCGv cpu_st0;
static TCGv cpu_st1;
static TCGv cpu_ifr;
static TCGv cpu_ier;
static TCGv cpu_dp;

/* ------------------------------------------------------------------ */
/* Framework-required helpers                                          */
/* ------------------------------------------------------------------ */

void gen_sync_pc(DisasContext *dc)
{
    tcg_gen_movi_tl(cpu_pc, dc->base.pc);
}

void setup_disas_context(DisasContextBase *dc, CPUState *env)
{
    dc->mem_idx = cpu_mmu_index(env);
}

static const char *const xar_names[8] = {
    "XAR0","XAR1","XAR2","XAR3","XAR4","XAR5","XAR6","XAR7"
};

/* ------------------------------------------------------------------ */
/* translate_init                                                      */
/* ------------------------------------------------------------------ */

void translate_init(void)
{
    cpu_acc = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, acc), "ACC");
    cpu_p   = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, p),   "P");
    cpu_xt  = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, xt),  "XT");

    for(int i = 0; i < 8; i++) {
        cpu_xar[i] = tcg_global_mem_new(TCG_AREG0,
                                        offsetof(CPUState, xar[i]),
                                        xar_names[i]);
    }

    cpu_sp  = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, sp),  "SP");
    cpu_pc  = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, pc),  "PC");
    cpu_rpc = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, rpc), "RPC");
    cpu_st0 = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, st0), "ST0");
    cpu_st1 = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, st1), "ST1");
    cpu_ifr = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, ifr), "IFR");
    cpu_ier = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, ier), "IER");
    cpu_dp  = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, dp),  "DP");
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
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static inline uint16_t fetch16(target_ulong pc)
{
    /* C2000 instruction words are 16-bit, byte-addressed */
    return (uint16_t)lduw_code(pc);
}

static inline void gen_jump(DisasContext *dc, target_ulong target)
{
    tcg_gen_movi_tl(cpu_pc, target);
    dc->base.is_jmp = DISAS_JUMP;
}

/* ------------------------------------------------------------------ */
/* Instruction decoder                                                 */
/*                                                                     */
/* C2000 instruction encoding overview:                               */
/*   Most instructions fit in 16 bits.  Encoding uses multiple       */
/*   formats; the top nibble broadly selects the instruction class.   */
/*                                                                     */
/* This skeleton handles the most common instructions found in        */
/* compiled C programs (via the TI CGT for C28x).                    */
/* Unrecognised opcodes raise EXCP_UNDEF.                             */
/* ------------------------------------------------------------------ */

static int disas_c2000_insn(CPUState *env, DisasContext *dc)
{
    target_ulong pc = dc->base.pc;
    uint16_t insn   = fetch16(pc);

    switch(insn >> 8)
    {
    /* ---- NOP (0x0000) ------------------------------------------- */
    case 0x00:
        if(insn == 0x0000) {
            /* NOP */
            break;
        }
        if(insn == 0x0006) {
            /* LRETR — Jump to RPC */
            tcg_gen_mov_tl(cpu_pc, cpu_rpc);
            dc->base.is_jmp = DISAS_JUMP;
            break;
        }
        /* IDLE: 0x0183 */
        if(insn == 0x0183) {
            gen_helper_idle(cpu_env);
            dc->base.is_jmp = DISAS_JUMP;
            break;
        }
        goto unimplemented;

    /* ---- MOVL ACC, #22bit (0x3A..3B) ----------------------------- */
    case 0x38: case 0x39: case 0x3A: case 0x3B: {
        /* MOV ACC, #16bit immediate (sign extended to 32) */
        uint16_t imm = fetch16(pc + 2);
        tcg_gen_movi_tl(cpu_acc, (int32_t)(int16_t)imm);
        dc->base.pc += 2;
        return 4;
    }

    /* ---- ADD ACC, #16bit (0x1C..1F) ------------------------------ */
    case 0x1C: case 0x1D: case 0x1E: case 0x1F: {
        uint16_t imm = fetch16(pc + 2);
        TCGv tmp = tcg_const_tl((int32_t)(int16_t)imm);
        tcg_gen_add_tl(cpu_acc, cpu_acc, tmp);
        tcg_temp_free(tmp);
        dc->base.pc += 2;
        return 4;
    }

    /* ---- SUB ACC, #16bit (0x20..23) ------------------------------ */
    case 0x20: case 0x21: case 0x22: case 0x23: {
        uint16_t imm = fetch16(pc + 2);
        TCGv tmp = tcg_const_tl((int32_t)(int16_t)imm);
        tcg_gen_sub_tl(cpu_acc, cpu_acc, tmp);
        tcg_temp_free(tmp);
        dc->base.pc += 2;
        return 4;
    }

    /* ---- MOVL XARn, #22bit (0x8Cn...) ---------------------------- */
    case 0x8C: case 0x8D: case 0x8E: case 0x8F:
    case 0xA8: case 0xA9: case 0xAA: case 0xAB: {
        /* MOV32 XARn, #22 — two-word instruction */
        uint8_t  n    = insn & 0x7;
        uint16_t lo   = fetch16(pc + 2);
        uint16_t hi_w = (uint16_t)(insn & 0x3F);
        uint32_t imm  = lo | ((uint32_t)(hi_w) << 16);
        tcg_gen_movi_tl(cpu_xar[n & 7], imm);
        dc->base.pc += 2;
        return 4;
    }

    /* ---- MOVL P, ACC (0x01xx) ------------------------------------ */
    case 0x01:
        if((insn & 0xFF) == 0x20) {
            tcg_gen_mov_tl(cpu_p, cpu_acc);
            break;
        }
        goto unimplemented;

    /* ---- MOVL ACC, P (0x01xx) ------------------------------------ */
    case 0x03:
        if((insn & 0xFF) == 0x20) {
            tcg_gen_mov_tl(cpu_acc, cpu_p);
            break;
        }
        goto unimplemented;

    /* ---- MOVL SP, XARn (0xE0xx) ---------------------------------- */
    case 0xE0:
        if((insn & 0xF8) == 0xC0) {
            uint8_t n = insn & 0x7;
            tcg_gen_andi_tl(cpu_sp, cpu_xar[n], 0xFFFF);
            break;
        }
        goto unimplemented;

    /* ---- LCR #22bit (long call) (0x7A00) ------------------------- */
    case 0x7A: {
        /* LCR #22 — two-word instruction; save PC+4 in RPC, then jump */
        uint16_t lo_w  = fetch16(pc + 2);
        uint16_t hi_w  = insn & 0x3F;
        uint32_t target = (uint32_t)lo_w | ((uint32_t)hi_w << 16);
        target <<= 1;   /* word address to byte address */
        TCGv retaddr = tcg_const_tl(pc + 4);
        tcg_gen_mov_tl(cpu_rpc, retaddr);
        tcg_temp_free(retaddr);
        gen_jump(dc, target);
        dc->base.pc += 2;
        return 4;
    }

    /* ---- B #offset, cond (0x56xx) -------------------------------- */
    case 0x56: {
        /* B offset,0 (unconditional short branch) */
        int8_t offset = (int8_t)(insn & 0xFF);
        target_ulong target = (target_ulong)((int64_t)(pc + 2) + offset * 2);
        gen_jump(dc, target);
        break;
    }

    /* ---- Default: unimplemented ---------------------------------- */
    default:
    unimplemented:
        tlib_printf(LOG_LEVEL_DEBUG,
                    "C2000: Unimplemented instruction 0x%04X at PC=0x%08X\n",
                    insn, (unsigned)pc);
        gen_helper_raise_exception(cpu_env, tcg_const_i32(EXCP_UNDEF));
        dc->base.is_jmp = DISAS_JUMP;
        break;
    }

    dc->base.pc += 2;
    return 2;
}

/* ------------------------------------------------------------------ */
/* gen_breakpoint                                                      */
/* ------------------------------------------------------------------ */

int gen_breakpoint(DisasContextBase *base, CPUBreakpoint *bp)
{
    (void)bp;
    gen_helper_raise_exception(cpu_env, tcg_const_i32(EXCP_UNDEF));
    gen_exit_tb_no_chaining(base->tb);
    base->is_jmp = DISAS_JUMP;
    return 1;
}

/* ------------------------------------------------------------------ */
/* gen_intermediate_code                                               */
/* ------------------------------------------------------------------ */

int gen_intermediate_code(CPUState *env, DisasContextBase *base)
{
    DisasContext *dc = (DisasContext *)base;

    tcg_gen_insn_start(base->pc);

    base->tb->size += disas_c2000_insn(env, dc);

    if((base->pc - base->tb->pc) != base->tb->size) {
        return 0;
    }
    if((base->pc & (TARGET_PAGE_SIZE - 1)) == 0) {
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* gen_intermediate_code_epilogue                                      */
/* ------------------------------------------------------------------ */

uint32_t gen_intermediate_code_epilogue(CPUState *env, DisasContextBase *base)
{
    DisasContext *dc = (DisasContext *)base;

    if(!dc->base.is_jmp) {
        gen_goto_tb(dc->base.tb, 0, dc->base.pc, dc->base.pc);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* restore_state_to_opc                                                */
/* ------------------------------------------------------------------ */

void restore_state_to_opc(CPUState *env, TranslationBlock *tb, target_ulong *data)
{
    env->pc = data[0];
}

/* ------------------------------------------------------------------ */
/* process_interrupt                                                   */
/* ------------------------------------------------------------------ */

int process_interrupt(int interrupt_request, CPUState *env)
{
    if(tlib_is_in_debug_mode()) {
        return 0;
    }

    if(interrupt_request & CPU_INTERRUPT_HARD) {
        if(cpu_interrupts_enabled(env)) {
            int32_t vec = tlib_find_best_interrupt();
            if(vec > 0) {
                env->exception_index = EXCP_PIE_INT + vec - 1;
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

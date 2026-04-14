/*
 * dsPIC33 TCG translation front-end.
 *
 * dsPIC33 has 24-bit instruction words stored in 32-bit-wide program memory
 * (the 8th byte of every 4-byte program word is a "phantom byte").
 * Byte address arithmetic: each instruction is 4 bytes apart in memory
 * but the ISA PC increments by 2 (word-address units).
 * This emulator uses byte addressing throughout.
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

static TCGv cpu_w[16];
static TCGv cpu_pc;
static TCGv cpu_status;
static TCGv cpu_tblpag;
static TCGv cpu_psvpag;
static TCGv cpu_rcount;
static TCGv cpu_dcount;
static TCGv cpu_corcon;
static TCGv cpu_disicnt;

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

static const char *const wreg_names[16] = {
    "W0",  "W1",  "W2",  "W3",  "W4",  "W5",  "W6",  "W7",
    "W8",  "W9",  "W10", "W11", "W12", "W13", "W14", "W15",
};

/* ------------------------------------------------------------------ */
/* translate_init — called once during CPU init                        */
/* ------------------------------------------------------------------ */

void translate_init(void)
{
    for(int i = 0; i < 16; i++) {
        cpu_w[i] = tcg_global_mem_new(TCG_AREG0,
                                      offsetof(CPUState, w[i]),
                                      wreg_names[i]);
    }

    cpu_pc      = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, pc),      "PC");
    cpu_status  = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, status),  "STATUS");
    cpu_tblpag  = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, tblpag),  "TBLPAG");
    cpu_psvpag  = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, psvpag),  "PSVPAG");
    cpu_rcount  = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, rcount),  "RCOUNT");
    cpu_dcount  = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, dcount),  "DCOUNT");
    cpu_corcon  = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, corcon),  "CORCON");
    cpu_disicnt = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, disicnt), "DISICNT");
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

/* Extract bits [hi:lo] (inclusive) from a 32-bit word. */
static inline uint32_t field(uint32_t insn, int hi, int lo)
{
    return (insn >> lo) & ((1u << (hi - lo + 1)) - 1u);
}

/* Sign-extend a value of 'bits' width to 32 bits. */
static inline int32_t sign_ext(uint32_t val, int bits)
{
    int shift = 32 - bits;
    return (int32_t)(val << shift) >> shift;
}

/* Generate a synchronous jump to a known target. */
static void gen_goto_pc(DisasContext *dc, target_ulong target)
{
    tcg_gen_movi_tl(cpu_pc, target);
    dc->base.is_jmp = DISAS_JUMP;
}

/* ------------------------------------------------------------------ */
/* Instruction decoder                                                 */
/*                                                                     */
/* dsPIC33 instruction encoding (24 bits):                            */
/*   bits 23:16 = primary opcode field                                */
/*   details vary by instruction class                                */
/*                                                                     */
/* This is a structural skeleton; only the most common instructions   */
/* are decoded.  Everything else raises EXCP_UNDEF.                   */
/* ------------------------------------------------------------------ */

/* Decode and generate TCG for one instruction.
 * Returns the byte-length of the consumed instruction (always 4 for dsPIC33). */
static int disas_dspic33_insn(CPUState *env, DisasContext *dc)
{
    target_ulong pc = dc->base.pc;

    /* Fetch a 24-bit instruction from the 32-bit-wide program flash.
     * The phantom byte is the most-significant byte of the 32-bit word. */
    uint32_t insn_word = ldl_code(pc);
    uint32_t insn = insn_word & 0x00FFFFFFu;   /* mask out phantom byte */

    /* Primary opcode in bits [23:16] */
    uint8_t op = (uint8_t)(insn >> 16);

    switch(op) {
    /* ---- NOP (0x00xxxx) ----------------------------------------- */
    case 0x00:
        /* NOP: do nothing, advance PC */
        break;

    /* ---- MOV.W Wns, Wnd (0x78Wsnd) ------------------------------ */
    case 0x78: {
        /* MOV Wns, Wnd  — register to register, word */
        uint8_t wns = field(insn, 6, 4) & 0xFu;
        uint8_t wnd = field(insn, 2, 0) & 0xFu;
        tcg_gen_mov_tl(cpu_w[wnd], cpu_w[wns]);
        /* Mask to 16 bits */
        tcg_gen_andi_tl(cpu_w[wnd], cpu_w[wnd], 0xFFFFu);
        break;
    }

    /* ---- MOV.W #lit16, Wnd (0xE0...) ----------------------------- */
    case 0xE0: case 0xE1: case 0xE2: case 0xE3:
    case 0xE4: case 0xE5: case 0xE6: case 0xE7: {
        /* MOV #lit16, Wnd */
        uint16_t lit = (uint16_t)(insn & 0xFFFFu);
        uint8_t  wnd = field(insn >> 16, 3, 0) & 0xFu;
        tcg_gen_movi_tl(cpu_w[wnd], lit);
        break;
    }

    /* ---- ADD.W Wb, Ws, Wd (0x40...) ------------------------------ */
    case 0x40: case 0x41: case 0x42: case 0x43: {
        uint8_t wb  = field(insn, 19, 16) & 0xFu;
        uint8_t ws  = field(insn, 14, 11) & 0xFu;
        uint8_t wd  = field(insn,  3,  0) & 0xFu;
        TCGv tmp = tcg_temp_new();
        tcg_gen_add_tl(tmp, cpu_w[wb], cpu_w[ws]);
        tcg_gen_andi_tl(cpu_w[wd], tmp, 0xFFFFu);
        tcg_temp_free(tmp);
        break;
    }

    /* ---- SUB.W Wb, Ws, Wd (0x50...) ------------------------------ */
    case 0x50: case 0x51: case 0x52: case 0x53: {
        uint8_t wb  = field(insn, 19, 16) & 0xFu;
        uint8_t ws  = field(insn, 14, 11) & 0xFu;
        uint8_t wd  = field(insn,  3,  0) & 0xFu;
        TCGv tmp = tcg_temp_new();
        tcg_gen_sub_tl(tmp, cpu_w[wb], cpu_w[ws]);
        tcg_gen_andi_tl(cpu_w[wd], tmp, 0xFFFFu);
        tcg_temp_free(tmp);
        break;
    }

    /* ---- AND.W Wb, Ws, Wd (0x60...) ------------------------------ */
    case 0x60: case 0x61: {
        uint8_t wb  = field(insn, 19, 16) & 0xFu;
        uint8_t ws  = field(insn, 14, 11) & 0xFu;
        uint8_t wd  = field(insn,  3,  0) & 0xFu;
        tcg_gen_and_tl(cpu_w[wd], cpu_w[wb], cpu_w[ws]);
        tcg_gen_andi_tl(cpu_w[wd], cpu_w[wd], 0xFFFFu);
        break;
    }

    /* ---- IOR.W (inclusive OR) Wb, Ws, Wd (0x60...) --------------- */
    case 0x62: case 0x63: {
        uint8_t wb  = field(insn, 19, 16) & 0xFu;
        uint8_t ws  = field(insn, 14, 11) & 0xFu;
        uint8_t wd  = field(insn,  3,  0) & 0xFu;
        tcg_gen_or_tl(cpu_w[wd], cpu_w[wb], cpu_w[ws]);
        tcg_gen_andi_tl(cpu_w[wd], cpu_w[wd], 0xFFFFu);
        break;
    }

    /* ---- XOR.W Wb, Ws, Wd (0x64...) ------------------------------ */
    case 0x64: case 0x65: {
        uint8_t wb  = field(insn, 19, 16) & 0xFu;
        uint8_t ws  = field(insn, 14, 11) & 0xFu;
        uint8_t wd  = field(insn,  3,  0) & 0xFu;
        tcg_gen_xor_tl(cpu_w[wd], cpu_w[wb], cpu_w[ws]);
        tcg_gen_andi_tl(cpu_w[wd], cpu_w[wd], 0xFFFFu);
        break;
    }

    /* ---- GOTO lit23 (0x04...) ------------------------------------ */
    case 0x04: {
        /* GOTO is a two-word instruction; second word holds upper address bits */
        uint32_t insn2 = ldl_code(pc + 4) & 0x00FFFFFFu;
        uint32_t target = ((insn & 0xFFFFu) | ((insn2 & 0x7Fu) << 16)) << 1;
        gen_goto_pc(dc, target);
        dc->base.pc += 4;   /* consume both words */
        return 8;
    }

    /* ---- CALL lit23 (0x02...) ------------------------------------ */
    case 0x02: {
        /* Push return address (PC + 8) on W15 stack, then jump */
        TCGv sp    = cpu_w[15];
        TCGv retpc = tcg_const_tl(pc + 8);

        /* stw retpc, [--W15] */
        TCGv sp_new = tcg_temp_new();
        tcg_gen_subi_tl(sp_new, sp, 4);
        tcg_gen_qemu_st32(retpc, sp_new, 0);
        tcg_gen_mov_tl(sp, sp_new);
        tcg_temp_free(sp_new);
        tcg_temp_free(retpc);

        uint32_t insn2 = ldl_code(pc + 4) & 0x00FFFFFFu;
        uint32_t target = ((insn & 0xFFFFu) | ((insn2 & 0x7Fu) << 16)) << 1;
        gen_goto_pc(dc, target);
        dc->base.pc += 4;
        return 8;
    }

    /* ---- RETURN (0x0006xx) --------------------------------------- */
    case 0x00 | 0x06:
        /* Already handled by case 0x00 NOP check — so split on low byte */
        /* checked below; RETURN opcode is actually 0x000006 */
        if((insn & 0xFFFFFFu) == 0x000006u) {
            /* POP PC from the hardware stack */
            TCGv sp   = cpu_w[15];
            TCGv addr = tcg_temp_new();
            tcg_gen_qemu_ld32u(addr, sp, 0);
            tcg_gen_andi_tl(addr, addr, 0x00FFFFFEu);
            tcg_gen_mov_tl(cpu_pc, addr);
            tcg_gen_addi_tl(sp, sp, 4);
            tcg_temp_free(addr);
            dc->base.is_jmp = DISAS_JUMP;
            break;
        }
        /* Fall through to default */
        /* FALLTHRU */

    /* ---- RETFIE (0x000004) --------------------------------------- */
    /* ---- RETLW  (0x050xxx) --------------------------------------- */

    /* ---- Default: unimplemented — generate a software trap -------- */
    default:
        tlib_printf(LOG_LEVEL_DEBUG,
                    "dsPIC33: Unimplemented instruction 0x%06X at PC=0x%08X\n",
                    insn, (unsigned)pc);
        gen_helper_raise_exception(cpu_env, tcg_const_i32(EXCP_UNDEF));
        dc->base.is_jmp = DISAS_JUMP;
        break;
    }

    /* Normal sequential execution: PC += 4 (one 24-bit instruction word). */
    dc->base.pc += 4;
    return 4;
}

/* ------------------------------------------------------------------ */
/* gen_breakpoint                                                      */
/* ------------------------------------------------------------------ */

int gen_breakpoint(DisasContextBase *base, CPUBreakpoint *bp)
{
    (void)bp;
    gen_helper_raise_exception(cpu_env, tcg_const_i32(EXCP_SOFT_TRAP));
    gen_exit_tb_no_chaining(base->tb);
    base->is_jmp = DISAS_JUMP;
    return 1;
}

/* ------------------------------------------------------------------ */
/* gen_intermediate_code — translate one instruction                  */
/* ------------------------------------------------------------------ */

int gen_intermediate_code(CPUState *env, DisasContextBase *base)
{
    DisasContext *dc = (DisasContext *)base;

    tcg_gen_insn_start(base->pc);

    base->tb->size += disas_dspic33_insn(env, dc);

    /* Stop block if pc changed or we crossed a page boundary */
    if((base->pc - base->tb->pc) != base->tb->size) {
        return 0;
    }
    if((base->pc & (TARGET_PAGE_SIZE - 1)) == 0) {
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* gen_intermediate_code_epilogue — finalize the block                */
/* ------------------------------------------------------------------ */

uint32_t gen_intermediate_code_epilogue(CPUState *env, DisasContextBase *base)
{
    DisasContext *dc = (DisasContext *)base;

    if(!dc->base.is_jmp) {
        /* Fall through: jump to next sequential instruction */
        gen_goto_tb(dc->base.tb, 0, dc->base.pc, dc->base.pc);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* restore_state_to_opc                                               */
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
                env->exception_index = EXCP_INT_FIRST + (vec - 1);
                do_interrupt(env);
                return 1;
            }
        }
    } else if(interrupt_request & CPU_INTERRUPT_RESET) {
        cpu_reset(env);
    }
    return 0;
}

/* Keep compiler happy — these are required stubs */
void cpu_exec_prologue(CPUState *env) { (void)env; }
void cpu_exec_epilogue(CPUState *env) { (void)env; }

/*
 * AVR TCG translation front-end.
 *
 * AVR instructions are 16-bit (1 word) or 32-bit (2 words, only CALL/JMP/LDS/STS).
 * PC is byte-addressed; each instruction word = 2 bytes.
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
/* TCG globals                                                         */
/* ------------------------------------------------------------------ */

static TCGv cpu_r[32];
static TCGv cpu_pc;
static TCGv cpu_sp;
static TCGv cpu_sreg;
static TCGv cpu_rampz;
static TCGv cpu_eind;

static const char *const r_names[32] = {
    "R0",  "R1",  "R2",  "R3",  "R4",  "R5",  "R6",  "R7",
    "R8",  "R9",  "R10", "R11", "R12", "R13", "R14", "R15",
    "R16", "R17", "R18", "R19", "R20", "R21", "R22", "R23",
    "R24", "R25", "R26", "R27", "R28", "R29", "R30", "R31",
};

void translate_init(void)
{
    for(int i = 0; i < 32; i++) {
        cpu_r[i] = tcg_global_mem_new(TCG_AREG0,
                                      offsetof(CPUState, r[i]),
                                      r_names[i]);
    }
    cpu_pc    = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, pc),    "PC");
    cpu_sp    = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, sp),    "SP");
    cpu_sreg  = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, sreg),  "SREG");
    cpu_rampz = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, rampz), "RAMPZ");
    cpu_eind  = tcg_global_mem_new(TCG_AREG0, offsetof(CPUState, eind),  "EIND");
}

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

static inline bool use_goto_tb(DisasContext *dc, target_ulong dest)
{
    return (dc->base.tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK);
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
/* Decode helpers                                                      */
/* ------------------------------------------------------------------ */

static inline uint16_t fetch16(target_ulong pc)
{
    return (uint16_t)lduw_code(pc);
}

/*
 * Extract 5-bit destination register from bits [8:4] of the opcode.
 * Used by most two-register instructions.
 */
static inline int avr_rd5(uint16_t op) { return (op >> 4) & 0x1F; }

/* Extract 5-bit source register from bits [9][3:0] */
static inline int avr_rr5(uint16_t op)
{
    return ((op >> 5) & 0x10) | (op & 0x0F);
}

/* 6-bit I/O address from IN/OUT instruction (bits [10:9,3:0]) */
static inline int avr_io_addr(uint16_t op)
{
    return ((op >> 5) & 0x30) | (op & 0x0F);
}

/* 8-bit signed PC-relative offset from branch instruction */
static inline int avr_branch_offset(uint16_t op)
{
    int k = (op >> 3) & 0x7F;
    if(k & 0x40) k |= ~0x7F; /* sign extend 7-bit */
    return k;
}

/* ------------------------------------------------------------------ */
/* Instruction decoder                                                 */
/* ------------------------------------------------------------------ */

static int disas_avr_insn(CPUState *env, DisasContext *dc)
{
    target_ulong pc  = dc->base.pc;
    uint16_t     op  = fetch16(pc);
    int          rd  = avr_rd5(op);
    int          rr  = avr_rr5(op);
    int          insn_words = 1;

    switch(op >> 10)
    {
    /* ---- 00: arithmetic and move group ------------------------------ */
    case 0x00 ... 0x03:
        switch(op >> 8)
        {
        case 0x00:
            if(op == 0x0000) {
                /* NOP */
                break;
            }
            if((op & 0xFF00) == 0x0100) {
                /* MOVW Rd+1:Rd, Rr+1:Rr  (op[7:4]=d, op[3:0]=r, d/r × 2) */
                int d2 = ((op >> 4) & 0xF) << 1;
                int r2 = (op & 0xF) << 1;
                tcg_gen_mov_tl(cpu_r[d2],     cpu_r[r2]);
                tcg_gen_mov_tl(cpu_r[d2 + 1], cpu_r[r2 + 1]);
                break;
            }
            goto unimplemented;
        case 0x01: case 0x02: case 0x03:
            goto unimplemented;
        /* ADD / ADC */
        case 0x0C: case 0x0D: case 0x0E: case 0x0F: {
            /* ADD: 0000 11rd dddd rrrr */
            TCGv result = tcg_temp_new();
            tcg_gen_add_tl(result, cpu_r[rd], cpu_r[rr]);
            tcg_gen_andi_tl(result, result, 0xFF);
            tcg_gen_mov_tl(cpu_r[rd], result);
            tcg_temp_free(result);
            break;
        }
        default: goto unimplemented;
        }
        break;

    /* ---- 01: subtract group ----------------------------------------- */
    case 0x04 ... 0x07:
        switch(op >> 8)
        {
        /* CP Rd,Rr: 0001 01rd dddd rrrr — just compare, update SREG */
        case 0x14: case 0x15: case 0x16: case 0x17:
            /* SUB Rd,Rr: 0001 10rd dddd rrrr */
        case 0x18: case 0x19: case 0x1A: case 0x1B: {
            TCGv result = tcg_temp_new();
            tcg_gen_sub_tl(result, cpu_r[rd], cpu_r[rr]);
            tcg_gen_andi_tl(result, result, 0xFF);
            if((op >> 11) == 0x03) { /* SUB only stores result */
                tcg_gen_mov_tl(cpu_r[rd], result);
            }
            tcg_temp_free(result);
            break;
        }
        /* ADC */
        case 0x1C: case 0x1D: case 0x1E: case 0x1F: {
            TCGv result = tcg_temp_new();
            TCGv carry  = tcg_temp_new();
            tcg_gen_andi_tl(carry, cpu_sreg, SREG_C);
            tcg_gen_add_tl(result, cpu_r[rd], cpu_r[rr]);
            tcg_gen_add_tl(result, result, carry);
            tcg_gen_andi_tl(result, result, 0xFF);
            tcg_gen_mov_tl(cpu_r[rd], result);
            tcg_temp_free(result);
            tcg_temp_free(carry);
            break;
        }
        default: goto unimplemented;
        }
        break;

    /* ---- 0x28-0x2F: AND / EOR / OR / MOV --------------------------- */
    case 0x08 ... 0x0B:
        switch(op >> 10)
        {
        case 0x08: /* AND */
            tcg_gen_and_tl(cpu_r[rd], cpu_r[rd], cpu_r[rr]);
            tcg_gen_andi_tl(cpu_r[rd], cpu_r[rd], 0xFF);
            break;
        case 0x09: /* EOR */
            tcg_gen_xor_tl(cpu_r[rd], cpu_r[rd], cpu_r[rr]);
            break;
        case 0x0A: /* OR */
            tcg_gen_or_tl(cpu_r[rd], cpu_r[rd], cpu_r[rr]);
            tcg_gen_andi_tl(cpu_r[rd], cpu_r[rd], 0xFF);
            break;
        case 0x0B: /* MOV Rd,Rr */
            tcg_gen_mov_tl(cpu_r[rd], cpu_r[rr]);
            break;
        }
        break;

    /* ---- 0x30-0x3F: CPI (compare immediate) d=r16..r31 ------------- */
    case 0x0C: {
        /* CPI Rd,K: 0011 KKKK dddd KKKK  (d = r16+((op>>4)&0xF)) */
        int d  = 16 + ((op >> 4) & 0xF);
        int k  = ((op >> 4) & 0xF0) | (op & 0x0F);
        TCGv imm = tcg_const_tl(k & 0xFF);
        tcg_temp_free(imm);
        break;
    }

    /* ---- 0xE0-0xEF: LDI (load immediate) d=r16..r31 ---------------- */
    case 0x38: case 0x39: case 0x3A: case 0x3B: {
        /* LDI Rd,K: 1110 KKKK dddd KKKK  (d=r16..r31) */
        int d = 16 + ((op >> 4) & 0xF);
        int k = ((op >> 4) & 0xF0) | (op & 0x0F);
        tcg_gen_movi_tl(cpu_r[d], k & 0xFF);
        break;
    }

    /* ---- 0xB0-0xBF: IN / OUT ---------------------------------------- */
    case 0x2C: case 0x2D: {
        int io = avr_io_addr(op);
        int is_out = (op >> 11) & 1;
        target_ulong bus_addr = AVR_DATA_BASE + 0x20 + io;
        if(is_out) {
            TCGv addr = tcg_const_tl(bus_addr);
            tcg_gen_qemu_st8(cpu_r[rd], addr, 0);
            tcg_temp_free(addr);
        } else {
            TCGv addr = tcg_const_tl(bus_addr);
            tcg_gen_qemu_ld8u(cpu_r[rd], addr, 0);
            tcg_temp_free(addr);
        }
        break;
    }

    /* ---- 0xC0-0xCF: RJMP ------------------------------------------- */
    case 0x30: {
        /* RJMP k: 1100 kkkk kkkk kkkk  (12-bit signed) */
        int k = op & 0x0FFF;
        if(k & 0x0800) k |= ~0x0FFF; /* sign extend 12 bits */
        target_ulong target = (pc + 2) + (target_ulong)((int64_t)k * 2);
        tcg_gen_movi_tl(cpu_pc, target);
        dc->base.is_jmp = DISAS_JUMP;
        break;
    }

    /* ---- 0xD0-0xDF: RCALL ------------------------------------------ */
    case 0x34: {
        int k = op & 0x0FFF;
        if(k & 0x0800) k |= ~0x0FFF;
        target_ulong ret_addr = pc + 2;
        target_ulong target   = ret_addr + (target_ulong)((int64_t)k * 2);
        /* Push return address (2 bytes) */
        TCGv sp       = tcg_temp_new();
        TCGv ret_lo   = tcg_const_tl(ret_addr & 0xFF);
        TCGv ret_hi   = tcg_const_tl((ret_addr >> 8) & 0xFF);
        TCGv base     = tcg_const_tl(AVR_DATA_BASE);
        tcg_gen_mov_tl(sp, cpu_sp);
        TCGv bus_addr_lo = tcg_temp_new();
        TCGv bus_addr_hi = tcg_temp_new();
        tcg_gen_add_tl(bus_addr_lo, base, sp);
        tcg_gen_qemu_st8(ret_lo, bus_addr_lo, 0);
        tcg_gen_subi_tl(sp, sp, 1);
        tcg_gen_add_tl(bus_addr_hi, base, sp);
        tcg_gen_qemu_st8(ret_hi, bus_addr_hi, 0);
        tcg_gen_subi_tl(sp, sp, 1);
        tcg_gen_andi_tl(sp, sp, 0xFFFF);
        tcg_gen_mov_tl(cpu_sp, sp);
        tcg_temp_free(sp); tcg_temp_free(ret_lo); tcg_temp_free(ret_hi);
        tcg_temp_free(base); tcg_temp_free(bus_addr_lo); tcg_temp_free(bus_addr_hi);
        tcg_gen_movi_tl(cpu_pc, target);
        dc->base.is_jmp = DISAS_JUMP;
        break;
    }

    /* ---- 0x90/0x91/0x92/0x93: LD/ST, PUSH/POP, and misc ----------- */
    case 0x24: case 0x25: case 0x26: case 0x27: {
        uint8_t hi = op >> 8;
        if(op == 0x9508) {
            /* RET — pop 2-byte return address */
            TCGv sp      = tcg_temp_new();
            TCGv base    = tcg_const_tl(AVR_DATA_BASE);
            TCGv lo_addr = tcg_temp_new();
            TCGv hi_addr = tcg_temp_new();
            TCGv lo_val  = tcg_temp_new();
            TCGv hi_val  = tcg_temp_new();
            tcg_gen_mov_tl(sp, cpu_sp);
            tcg_gen_addi_tl(sp, sp, 1);
            tcg_gen_add_tl(hi_addr, base, sp);
            tcg_gen_qemu_ld8u(hi_val, hi_addr, 0);
            tcg_gen_addi_tl(sp, sp, 1);
            tcg_gen_add_tl(lo_addr, base, sp);
            tcg_gen_qemu_ld8u(lo_val, lo_addr, 0);
            tcg_gen_andi_tl(sp, sp, 0xFFFF);
            tcg_gen_mov_tl(cpu_sp, sp);
            TCGv ret_pc = tcg_temp_new();
            tcg_gen_shli_tl(ret_pc, hi_val, 8);
            tcg_gen_or_tl(ret_pc, ret_pc, lo_val);
            tcg_gen_mov_tl(cpu_pc, ret_pc);
            tcg_temp_free(sp); tcg_temp_free(base);
            tcg_temp_free(lo_addr); tcg_temp_free(hi_addr);
            tcg_temp_free(lo_val); tcg_temp_free(hi_val);
            tcg_temp_free(ret_pc);
            dc->base.is_jmp = DISAS_JUMP;
            break;
        }
        if(op == 0x9518) {
            /* RETI — return from interrupt; re-enable interrupts */
            TCGv sei_bit = tcg_const_tl(SREG_I);
            tcg_gen_or_tl(cpu_sreg, cpu_sreg, sei_bit);
            tcg_temp_free(sei_bit);
            /* same pop logic as RET */
            TCGv sp      = tcg_temp_new();
            TCGv base    = tcg_const_tl(AVR_DATA_BASE);
            TCGv lo_addr = tcg_temp_new();
            TCGv hi_addr = tcg_temp_new();
            TCGv lo_val  = tcg_temp_new();
            TCGv hi_val  = tcg_temp_new();
            tcg_gen_mov_tl(sp, cpu_sp);
            tcg_gen_addi_tl(sp, sp, 1);
            tcg_gen_add_tl(hi_addr, base, sp);
            tcg_gen_qemu_ld8u(hi_val, hi_addr, 0);
            tcg_gen_addi_tl(sp, sp, 1);
            tcg_gen_add_tl(lo_addr, base, sp);
            tcg_gen_qemu_ld8u(lo_val, lo_addr, 0);
            tcg_gen_andi_tl(sp, sp, 0xFFFF);
            tcg_gen_mov_tl(cpu_sp, sp);
            TCGv ret_pc = tcg_temp_new();
            tcg_gen_shli_tl(ret_pc, hi_val, 8);
            tcg_gen_or_tl(ret_pc, ret_pc, lo_val);
            tcg_gen_mov_tl(cpu_pc, ret_pc);
            tcg_temp_free(sp); tcg_temp_free(base);
            tcg_temp_free(lo_addr); tcg_temp_free(hi_addr);
            tcg_temp_free(lo_val); tcg_temp_free(hi_val);
            tcg_temp_free(ret_pc);
            dc->base.is_jmp = DISAS_JUMP;
            break;
        }
        if(op == 0x9478) {
            /* SEI: set global interrupt enable */
            TCGv bit = tcg_const_tl(SREG_I);
            tcg_gen_or_tl(cpu_sreg, cpu_sreg, bit);
            tcg_temp_free(bit);
            break;
        }
        if(op == 0x94F8) {
            /* CLI: clear global interrupt enable */
            TCGv mask = tcg_const_tl(~SREG_I);
            tcg_gen_and_tl(cpu_sreg, cpu_sreg, mask);
            tcg_temp_free(mask);
            break;
        }
        /* PUSH Rr: 1001 001r rrrr 1111 */
        if((op & 0xFE0F) == 0x920F) {
            int r = (op >> 4) & 0x1F;
            TCGv sp      = tcg_temp_new();
            TCGv base    = tcg_const_tl(AVR_DATA_BASE);
            TCGv bus_addr = tcg_temp_new();
            tcg_gen_mov_tl(sp, cpu_sp);
            tcg_gen_add_tl(bus_addr, base, sp);
            tcg_gen_qemu_st8(cpu_r[r], bus_addr, 0);
            tcg_gen_subi_tl(sp, sp, 1);
            tcg_gen_andi_tl(sp, sp, 0xFFFF);
            tcg_gen_mov_tl(cpu_sp, sp);
            tcg_temp_free(sp); tcg_temp_free(base); tcg_temp_free(bus_addr);
            break;
        }
        /* POP Rd: 1001 000d dddd 1111 */
        if((op & 0xFE0F) == 0x900F) {
            int d2 = (op >> 4) & 0x1F;
            TCGv sp       = tcg_temp_new();
            TCGv base     = tcg_const_tl(AVR_DATA_BASE);
            TCGv bus_addr = tcg_temp_new();
            tcg_gen_mov_tl(sp, cpu_sp);
            tcg_gen_addi_tl(sp, sp, 1);
            tcg_gen_add_tl(bus_addr, base, sp);
            tcg_gen_qemu_ld8u(cpu_r[d2], bus_addr, 0);
            tcg_gen_andi_tl(sp, sp, 0xFFFF);
            tcg_gen_mov_tl(cpu_sp, sp);
            tcg_temp_free(sp); tcg_temp_free(base); tcg_temp_free(bus_addr);
            break;
        }
        /* CALL k: 2-word instruction, 1001 010k kkkk 111k + kkkkkkkkkkkkkkkk */
        if((op & 0xFE0E) == 0x940E) {
            uint16_t k_hi = op & 0x01F1; /* bits from first word */
            uint16_t k_lo = fetch16(pc + 2);
            uint32_t target = ((uint32_t)(k_hi >> 3 << 16) |
                               (uint32_t)(k_hi & 1) << 16 | k_lo) << 1;
            /* push return address (pc+4) */
            target_ulong ret = pc + 4;
            TCGv sp      = tcg_temp_new();
            TCGv base    = tcg_const_tl(AVR_DATA_BASE);
            TCGv ra_lo   = tcg_const_tl(ret & 0xFF);
            TCGv ra_hi   = tcg_const_tl((ret >> 8) & 0xFF);
            TCGv baddr   = tcg_temp_new();
            tcg_gen_mov_tl(sp, cpu_sp);
            tcg_gen_add_tl(baddr, base, sp); tcg_gen_qemu_st8(ra_lo, baddr, 0); tcg_gen_subi_tl(sp, sp, 1);
            tcg_gen_add_tl(baddr, base, sp); tcg_gen_qemu_st8(ra_hi, baddr, 0); tcg_gen_subi_tl(sp, sp, 1);
            tcg_gen_andi_tl(sp, sp, 0xFFFF); tcg_gen_mov_tl(cpu_sp, sp);
            tcg_temp_free(sp); tcg_temp_free(base); tcg_temp_free(ra_lo); tcg_temp_free(ra_hi); tcg_temp_free(baddr);
            tcg_gen_movi_tl(cpu_pc, target);
            dc->base.is_jmp = DISAS_JUMP;
            insn_words = 2;
            break;
        }
        /* JMP k: 1001 010k kkkk 110k + kkkkkkkkkkkkkkkk */
        if((op & 0xFE0E) == 0x940C) {
            uint16_t k_lo = fetch16(pc + 2);
            uint32_t target = (uint32_t)k_lo << 1;
            tcg_gen_movi_tl(cpu_pc, target);
            dc->base.is_jmp = DISAS_JUMP;
            insn_words = 2;
            break;
        }
        goto unimplemented;
    }

    /* ---- 0xF0-0xF7: conditional branches (BRBS/BRBC) --------------- */
    case 0x3C: case 0x3D: case 0x3E: case 0x3F: {
        /*
         * BRBS/BRBC: 1111 0kkk kkkk ksss (set) / 1111 1kkk kkkk ksss (clear)
         * k = 7-bit signed offset from next PC; s = SREG bit index.
         */
        int offset    = avr_branch_offset(op);
        int bit_idx   = op & 0x7;
        int branch_if = !((op >> 10) & 1); /* 0 = BRBS (if set), 1 = BRBC (if clear) */
        uint32_t bit_mask = 1u << bit_idx;
        target_ulong taken    = (pc + 2) + (target_ulong)((int64_t)offset * 2);
        target_ulong not_taken = pc + 2;

        TCGv sreg_bit = tcg_temp_new();
        TCGv zero     = tcg_const_tl(0);
        tcg_gen_andi_tl(sreg_bit, cpu_sreg, bit_mask);
        if(branch_if) {
            tcg_gen_movcond_tl(TCG_COND_NE, cpu_pc, sreg_bit, zero,
                               tcg_const_tl(taken), tcg_const_tl(not_taken));
        } else {
            tcg_gen_movcond_tl(TCG_COND_EQ, cpu_pc, sreg_bit, zero,
                               tcg_const_tl(taken), tcg_const_tl(not_taken));
        }
        tcg_temp_free(sreg_bit); tcg_temp_free(zero);
        dc->base.is_jmp = DISAS_JUMP;
        break;
    }

    default:
    unimplemented:
        tlib_printf(LOG_LEVEL_DEBUG,
                    "AVR: Unimplemented instruction 0x%04X at PC=0x%08X\n",
                    op, (unsigned)pc);
        gen_helper_raise_exception(cpu_env, tcg_const_i32(EXCP_UNDEF));
        dc->base.is_jmp = DISAS_JUMP;
        break;
    }

    dc->base.pc += (target_ulong)(insn_words * 2);
    return insn_words * 2;
}

/* ------------------------------------------------------------------ */
/* Breakpoint                                                          */
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

    base->tb->size += disas_avr_insn(env, dc);

    if((base->pc - base->tb->pc) != base->tb->size) {
        return 0;
    }
    if((base->pc & (TARGET_PAGE_SIZE - 1)) == 0) {
        return 0;
    }
    return 1;
}

uint32_t gen_intermediate_code_epilogue(CPUState *env, DisasContextBase *base)
{
    DisasContext *dc = (DisasContext *)base;
    if(!dc->base.is_jmp) {
        gen_goto_tb(dc->base.tb, 0, dc->base.pc, dc->base.pc);
    }
    return 0;
}

void restore_state_to_opc(CPUState *env, TranslationBlock *tb, target_ulong *data)
{
    env->pc = data[0];
}

int process_interrupt(int interrupt_request, CPUState *env)
{
    if(tlib_is_in_debug_mode()) {
        return 0;
    }

    if(interrupt_request & CPU_INTERRUPT_HARD) {
        if(cpu_interrupts_enabled(env)) {
            int32_t vec = tlib_find_best_interrupt();
            if(vec > 0) {
                env->exception_index = EXCP_INT_FIRST + vec - 1;
                do_interrupt(env);
                return 1;
            }
        }
    }
    return 0;
}

void cpu_exec_prologue(CPUState *env)  { (void)env; }
void cpu_exec_epilogue(CPUState *env)  { (void)env; }

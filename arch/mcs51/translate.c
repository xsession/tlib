/*
 * 8051 (MCS-51) TCG instruction translator.
 *
 * 8051 instruction set: variable-length 1/2/3 byte instructions.
 * Three address spaces: CODE (program), IRAM (internal data 0-7Fh direct,
 * 80h-FFh indirect), SFR (80h-FFh direct), XDATA (64 KB external).
 *
 * Instruction groups:
 *   Arithmetic: ADD, ADDC, SUBB, INC, DEC, MUL, DIV, DA
 *   Logical:    ANL, ORL, XRL, CLR, CPL, RL, RLC, RR, RRC, SWAP
 *   Transfer:   MOV, MOVC, MOVX, XCH, XCHD, PUSH, POP
 *   Boolean:    CLR/SETB/CPL bit, ANL/ORL C,bit, JB, JNB, JBC
 *   Branch:     ACALL, LCALL, RET, RETI, AJMP, LJMP, SJMP, JZ, JNZ, JC, JNC,
 *               CJNE, DJNZ
 *   Special:    NOP
 *
 * Copyright (c) Antmicro – MIT License
 */
#include "cpu.h"
#include "arch_callbacks.h"
#include "tb-helper.h"
#include "debug.h"

/* TCG globals — one per architectural register */
static TCGv cpu_acc;
static TCGv cpu_b;
static TCGv cpu_psw;
static TCGv cpu_sp;
static TCGv cpu_dpl;
static TCGv cpu_dph;
static TCGv cpu_pc;
static TCGv cpu_ie;
static TCGv cpu_ip;

/* gen_goto_tb and gen_exit_tb are provided by exec-all.h via cpu.h */

void translate_init(void)
{
#define OFF(f) offsetof(CPUState, f)
    cpu_acc = tcg_global_mem_new_i32(TCG_AREG0, OFF(acc), "ACC");
    cpu_b   = tcg_global_mem_new_i32(TCG_AREG0, OFF(b),   "B");
    cpu_psw = tcg_global_mem_new_i32(TCG_AREG0, OFF(psw), "PSW");
    cpu_sp  = tcg_global_mem_new_i32(TCG_AREG0, OFF(sp),  "SP");
    cpu_dpl = tcg_global_mem_new_i32(TCG_AREG0, OFF(dpl), "DPL");
    cpu_dph = tcg_global_mem_new_i32(TCG_AREG0, OFF(dph), "DPH");
    cpu_pc  = tcg_global_mem_new_i32(TCG_AREG0, OFF(pc),  "PC");
    cpu_ie  = tcg_global_mem_new_i32(TCG_AREG0, OFF(ie),  "IE");
    cpu_ip  = tcg_global_mem_new_i32(TCG_AREG0, OFF(ip),  "IP");
#undef OFF
}

void gen_sync_pc(DisasContext *dc)
{
    tcg_gen_movi_i32(cpu_pc, dc->base.pc);
}

void setup_disas_context(DisasContextBase *dc, CPUState *env)
{
    dc->mem_idx = cpu_mmu_index(env);
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

/* ---- Helpers: IRAM load/store ---- */
static void gen_iram_ld8(TCGv dst, TCGv addr8)
{
    TCGv full = tcg_temp_new_i32();
    tcg_gen_andi_i32(full, addr8, 0xFF);
    tcg_gen_addi_i32(full, full, MCS51_IRAM_BASE);
    tcg_gen_qemu_ld8u(dst, full, 0);
    tcg_temp_free_i32(full);
}

static void gen_iram_st8(TCGv addr8, TCGv val)
{
    TCGv full = tcg_temp_new_i32();
    tcg_gen_andi_i32(full, addr8, 0xFF);
    tcg_gen_addi_i32(full, full, MCS51_IRAM_BASE);
    tcg_gen_qemu_st8(val, full, 0);
    tcg_temp_free_i32(full);
}

/* SFR direct access (address 0x80-0xFF) */
static void gen_sfr_ld8(TCGv dst, uint8_t addr)
{
    TCGv a = tcg_const_i32(MCS51_SFR_BASE + addr);
    tcg_gen_qemu_ld8u(dst, a, 0);
    tcg_temp_free_i32(a);
}

static void gen_sfr_st8(uint8_t addr, TCGv val)
{
    TCGv a = tcg_const_i32(MCS51_SFR_BASE + addr);
    tcg_gen_qemu_st8(val, a, 0);
    tcg_temp_free_i32(a);
}

/* Direct address: < 0x80 → IRAM, >= 0x80 → SFR */
static void gen_direct_ld8(TCGv dst, uint8_t addr)
{
    if(addr < 0x80) {
        TCGv a = tcg_const_i32(addr);
        gen_iram_ld8(dst, a);
        tcg_temp_free_i32(a);
    } else {
        gen_sfr_ld8(dst, addr);
    }
}

static void gen_direct_st8(uint8_t addr, TCGv val)
{
    if(addr < 0x80) {
        TCGv a = tcg_const_i32(addr);
        gen_iram_st8(a, val);
        tcg_temp_free_i32(a);
    } else {
        gen_sfr_st8(addr, val);
    }
}

/* Update PSW carry and zero from result (simplified) */
static void gen_update_cz(TCGv result8)
{
    TCGv t = tcg_temp_new_i32();
    /* Z: if result == 0, but 8051 has no Z flag in PSW; skip. */
    /* C: bit 8 carries remain as-is in caller (for ADD/SUBB). */
    tcg_temp_free_i32(t);
}

/* ------------------------------------------------------------------ */
static int disas_mcs51_insn(CPUState *env, DisasContext *dc)
{
    uint32_t pc = dc->pc;
    uint8_t op = ldub_code(pc);
    dc->pc = pc + 1;

    TCGv tmp = tcg_temp_new_i32();
    TCGv tmp2 = tcg_temp_new_i32();

    switch(op) {

    /* ---- NOP ---- */
    case 0x00:
        break;

    /* ---- LJMP addr16 (0x02) ---- */
    case 0x02: {
        uint8_t hi = ldub_code(dc->pc++);
        uint8_t lo = ldub_code(dc->pc++);
        tcg_gen_movi_i32(cpu_pc, (hi << 8) | lo);
        break;
    }

    /* ---- RR A (0x03) ---- */
    case 0x03:
        tcg_gen_shri_i32(tmp, cpu_acc, 1);
        tcg_gen_shli_i32(tmp2, cpu_acc, 7);
        tcg_gen_or_i32(cpu_acc, tmp, tmp2);
        tcg_gen_andi_i32(cpu_acc, cpu_acc, 0xFF);
        break;

    /* ---- INC A (0x04) ---- */
    case 0x04:
        tcg_gen_addi_i32(cpu_acc, cpu_acc, 1);
        tcg_gen_andi_i32(cpu_acc, cpu_acc, 0xFF);
        break;

    /* ---- INC direct (0x05) ---- */
    case 0x05: {
        uint8_t addr = ldub_code(dc->pc++);
        gen_direct_ld8(tmp, addr);
        tcg_gen_addi_i32(tmp, tmp, 1);
        tcg_gen_andi_i32(tmp, tmp, 0xFF);
        gen_direct_st8(addr, tmp);
        break;
    }

    /* ---- INC @Ri (0x06-0x07) ---- */
    case 0x06: case 0x07: {
        int ri = op & 1;
        /* Ri is in register bank; bank addr = (PSW>>3)&0x18 + ri */
        TCGv bank = tcg_temp_new_i32();
        tcg_gen_shri_i32(bank, cpu_psw, 3);
        tcg_gen_andi_i32(bank, bank, 0x18);
        tcg_gen_addi_i32(bank, bank, ri);
        gen_iram_ld8(tmp, bank);       /* tmp = address pointed to by Ri */
        gen_iram_ld8(tmp2, tmp);       /* actual byte */
        tcg_gen_addi_i32(tmp2, tmp2, 1);
        tcg_gen_andi_i32(tmp2, tmp2, 0xFF);
        gen_iram_st8(tmp, tmp2);
        tcg_temp_free_i32(bank);
        break;
    }

    /* ---- INC Rn (0x08-0x0F) ---- */
    case 0x08: case 0x09: case 0x0A: case 0x0B:
    case 0x0C: case 0x0D: case 0x0E: case 0x0F: {
        int rn = op & 7;
        TCGv bank = tcg_temp_new_i32();
        tcg_gen_shri_i32(bank, cpu_psw, 3);
        tcg_gen_andi_i32(bank, bank, 0x18);
        tcg_gen_addi_i32(bank, bank, rn);
        gen_iram_ld8(tmp, bank);
        tcg_gen_addi_i32(tmp, tmp, 1);
        tcg_gen_andi_i32(tmp, tmp, 0xFF);
        gen_iram_st8(bank, tmp);
        tcg_temp_free_i32(bank);
        break;
    }

    /* ---- LCALL addr16 (0x12) ---- */
    case 0x12: {
        uint8_t hi = ldub_code(dc->pc++);
        uint8_t lo = ldub_code(dc->pc++);
        uint16_t target = (hi << 8) | lo;
        /* Push return address (dc->pc) onto stack */
        TCGv retl = tcg_const_i32(dc->pc & 0xFF);
        TCGv reth = tcg_const_i32((dc->pc >> 8) & 0xFF);
        tcg_gen_addi_i32(cpu_sp, cpu_sp, 1);
        tcg_gen_andi_i32(cpu_sp, cpu_sp, 0xFF);
        gen_iram_st8(cpu_sp, retl);
        tcg_gen_addi_i32(cpu_sp, cpu_sp, 1);
        tcg_gen_andi_i32(cpu_sp, cpu_sp, 0xFF);
        gen_iram_st8(cpu_sp, reth);
        tcg_gen_movi_i32(cpu_pc, target);
        tcg_temp_free_i32(retl);
        tcg_temp_free_i32(reth);
        break;
    }

    /* ---- RRC A (0x13) ---- */
    case 0x13: {
        TCGv old_c = tcg_temp_new_i32();
        tcg_gen_shri_i32(old_c, cpu_psw, 7); /* CY bit 7 */
        tcg_gen_andi_i32(old_c, old_c, 1);
        /* new CY = ACC bit 0 */
        tcg_gen_deposit_i32(cpu_psw, cpu_psw, cpu_acc, 7, 1);
        /* ACC = (old_c << 7) | (ACC >> 1) */
        tcg_gen_shri_i32(cpu_acc, cpu_acc, 1);
        tcg_gen_shli_i32(old_c, old_c, 7);
        tcg_gen_or_i32(cpu_acc, cpu_acc, old_c);
        tcg_gen_andi_i32(cpu_acc, cpu_acc, 0xFF);
        tcg_temp_free_i32(old_c);
        break;
    }

    /* ---- DEC A (0x14) ---- */
    case 0x14:
        tcg_gen_subi_i32(cpu_acc, cpu_acc, 1);
        tcg_gen_andi_i32(cpu_acc, cpu_acc, 0xFF);
        break;

    /* ---- DEC direct (0x15) ---- */
    case 0x15: {
        uint8_t addr = ldub_code(dc->pc++);
        gen_direct_ld8(tmp, addr);
        tcg_gen_subi_i32(tmp, tmp, 1);
        tcg_gen_andi_i32(tmp, tmp, 0xFF);
        gen_direct_st8(addr, tmp);
        break;
    }

    /* ---- DEC Rn (0x18-0x1F) ---- */
    case 0x18: case 0x19: case 0x1A: case 0x1B:
    case 0x1C: case 0x1D: case 0x1E: case 0x1F: {
        int rn = op & 7;
        TCGv bank = tcg_temp_new_i32();
        tcg_gen_shri_i32(bank, cpu_psw, 3);
        tcg_gen_andi_i32(bank, bank, 0x18);
        tcg_gen_addi_i32(bank, bank, rn);
        gen_iram_ld8(tmp, bank);
        tcg_gen_subi_i32(tmp, tmp, 1);
        tcg_gen_andi_i32(tmp, tmp, 0xFF);
        gen_iram_st8(bank, tmp);
        tcg_temp_free_i32(bank);
        break;
    }

    /* ---- RET (0x22) ---- */
    case 0x22: {
        /* Pop PCH then PCL from stack */
        gen_iram_ld8(tmp, cpu_sp);  /* PCH */
        tcg_gen_subi_i32(cpu_sp, cpu_sp, 1);
        tcg_gen_andi_i32(cpu_sp, cpu_sp, 0xFF);
        gen_iram_ld8(tmp2, cpu_sp); /* PCL */
        tcg_gen_subi_i32(cpu_sp, cpu_sp, 1);
        tcg_gen_andi_i32(cpu_sp, cpu_sp, 0xFF);
        tcg_gen_shli_i32(tmp, tmp, 8);
        tcg_gen_or_i32(cpu_pc, tmp, tmp2);
        break;
    }

    /* ---- RETI (0x32) ---- */
    case 0x32: {
        gen_iram_ld8(tmp, cpu_sp);
        tcg_gen_subi_i32(cpu_sp, cpu_sp, 1);
        tcg_gen_andi_i32(cpu_sp, cpu_sp, 0xFF);
        gen_iram_ld8(tmp2, cpu_sp);
        tcg_gen_subi_i32(cpu_sp, cpu_sp, 1);
        tcg_gen_andi_i32(cpu_sp, cpu_sp, 0xFF);
        tcg_gen_shli_i32(tmp, tmp, 8);
        tcg_gen_or_i32(cpu_pc, tmp, tmp2);
        /* re-enable interrupt level (handled by C# layer) */
        break;
    }

    /* ---- RL A (0x23) ---- */
    case 0x23:
        tcg_gen_shli_i32(tmp, cpu_acc, 1);
        tcg_gen_shri_i32(tmp2, cpu_acc, 7);
        tcg_gen_or_i32(cpu_acc, tmp, tmp2);
        tcg_gen_andi_i32(cpu_acc, cpu_acc, 0xFF);
        break;

    /* ---- ADD A,#data (0x24) ---- */
    case 0x24: {
        uint8_t imm = ldub_code(dc->pc++);
        tcg_gen_addi_i32(cpu_acc, cpu_acc, imm);
        /* set CY */
        tcg_gen_shri_i32(tmp, cpu_acc, 8);
        tcg_gen_deposit_i32(cpu_psw, cpu_psw, tmp, 7, 1);
        tcg_gen_andi_i32(cpu_acc, cpu_acc, 0xFF);
        break;
    }

    /* ---- ADD A,direct (0x25) ---- */
    case 0x25: {
        uint8_t addr = ldub_code(dc->pc++);
        gen_direct_ld8(tmp, addr);
        tcg_gen_add_i32(cpu_acc, cpu_acc, tmp);
        tcg_gen_shri_i32(tmp, cpu_acc, 8);
        tcg_gen_deposit_i32(cpu_psw, cpu_psw, tmp, 7, 1);
        tcg_gen_andi_i32(cpu_acc, cpu_acc, 0xFF);
        break;
    }

    /* ---- ADD A,Rn (0x28-0x2F) ---- */
    case 0x28: case 0x29: case 0x2A: case 0x2B:
    case 0x2C: case 0x2D: case 0x2E: case 0x2F: {
        int rn = op & 7;
        TCGv bank = tcg_temp_new_i32();
        tcg_gen_shri_i32(bank, cpu_psw, 3);
        tcg_gen_andi_i32(bank, bank, 0x18);
        tcg_gen_addi_i32(bank, bank, rn);
        gen_iram_ld8(tmp, bank);
        tcg_gen_add_i32(cpu_acc, cpu_acc, tmp);
        tcg_gen_shri_i32(tmp, cpu_acc, 8);
        tcg_gen_deposit_i32(cpu_psw, cpu_psw, tmp, 7, 1);
        tcg_gen_andi_i32(cpu_acc, cpu_acc, 0xFF);
        tcg_temp_free_i32(bank);
        break;
    }

    /* ---- SUBB A,#data (0x94) ---- */
    case 0x94: {
        uint8_t imm = ldub_code(dc->pc++);
        TCGv cy = tcg_temp_new_i32();
        tcg_gen_shri_i32(cy, cpu_psw, 7);
        tcg_gen_andi_i32(cy, cy, 1);
        tcg_gen_sub_i32(cpu_acc, cpu_acc, tcg_const_i32(imm));
        tcg_gen_sub_i32(cpu_acc, cpu_acc, cy);
        /* borrow → CY */
        tcg_gen_shri_i32(tmp, cpu_acc, 31); /* sign = borrow */
        tcg_gen_deposit_i32(cpu_psw, cpu_psw, tmp, 7, 1);
        tcg_gen_andi_i32(cpu_acc, cpu_acc, 0xFF);
        tcg_temp_free_i32(cy);
        break;
    }

    /* ---- ANL A,#data (0x54) ---- */
    case 0x54: {
        uint8_t imm = ldub_code(dc->pc++);
        tcg_gen_andi_i32(cpu_acc, cpu_acc, imm);
        break;
    }

    /* ---- ORL A,#data (0x44) ---- */
    case 0x44: {
        uint8_t imm = ldub_code(dc->pc++);
        tcg_gen_ori_i32(cpu_acc, cpu_acc, imm);
        break;
    }

    /* ---- XRL A,#data (0x64) ---- */
    case 0x64: {
        uint8_t imm = ldub_code(dc->pc++);
        tcg_gen_xori_i32(cpu_acc, cpu_acc, imm);
        break;
    }

    /* ---- MOV A,#data (0x74) ---- */
    case 0x74: {
        uint8_t imm = ldub_code(dc->pc++);
        tcg_gen_movi_i32(cpu_acc, imm);
        break;
    }

    /* ---- MOV direct,#data (0x75) ---- */
    case 0x75: {
        uint8_t addr = ldub_code(dc->pc++);
        uint8_t imm  = ldub_code(dc->pc++);
        TCGv v = tcg_const_i32(imm);
        gen_direct_st8(addr, v);
        tcg_temp_free_i32(v);
        break;
    }

    /* ---- MOV A,direct (0xE5) ---- */
    case 0xE5: {
        uint8_t addr = ldub_code(dc->pc++);
        gen_direct_ld8(cpu_acc, addr);
        break;
    }

    /* ---- MOV direct,A (0xF5) ---- */
    case 0xF5: {
        uint8_t addr = ldub_code(dc->pc++);
        gen_direct_st8(addr, cpu_acc);
        break;
    }

    /* ---- MOV A,Rn (0xE8-0xEF) ---- */
    case 0xE8: case 0xE9: case 0xEA: case 0xEB:
    case 0xEC: case 0xED: case 0xEE: case 0xEF: {
        int rn = op & 7;
        TCGv bank = tcg_temp_new_i32();
        tcg_gen_shri_i32(bank, cpu_psw, 3);
        tcg_gen_andi_i32(bank, bank, 0x18);
        tcg_gen_addi_i32(bank, bank, rn);
        gen_iram_ld8(cpu_acc, bank);
        tcg_temp_free_i32(bank);
        break;
    }

    /* ---- MOV Rn,A (0xF8-0xFF) ---- */
    case 0xF8: case 0xF9: case 0xFA: case 0xFB:
    case 0xFC: case 0xFD: case 0xFE: case 0xFF: {
        int rn = op & 7;
        TCGv bank = tcg_temp_new_i32();
        tcg_gen_shri_i32(bank, cpu_psw, 3);
        tcg_gen_andi_i32(bank, bank, 0x18);
        tcg_gen_addi_i32(bank, bank, rn);
        gen_iram_st8(bank, cpu_acc);
        tcg_temp_free_i32(bank);
        break;
    }

    /* ---- MOV Rn,#data (0x78-0x7F) ---- */
    case 0x78: case 0x79: case 0x7A: case 0x7B:
    case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
        int rn = op & 7;
        uint8_t imm = ldub_code(dc->pc++);
        TCGv bank = tcg_temp_new_i32();
        TCGv v = tcg_const_i32(imm);
        tcg_gen_shri_i32(bank, cpu_psw, 3);
        tcg_gen_andi_i32(bank, bank, 0x18);
        tcg_gen_addi_i32(bank, bank, rn);
        gen_iram_st8(bank, v);
        tcg_temp_free_i32(bank);
        tcg_temp_free_i32(v);
        break;
    }

    /* ---- MOV DPTR,#data16 (0x90) ---- */
    case 0x90: {
        uint8_t hi = ldub_code(dc->pc++);
        uint8_t lo = ldub_code(dc->pc++);
        tcg_gen_movi_i32(cpu_dph, hi);
        tcg_gen_movi_i32(cpu_dpl, lo);
        break;
    }

    /* ---- CLR A (0xE4) ---- */
    case 0xE4:
        tcg_gen_movi_i32(cpu_acc, 0);
        break;

    /* ---- CLR C (0xC3) ---- */
    case 0xC3:
        tcg_gen_andi_i32(cpu_psw, cpu_psw, ~PSW_CY);
        break;

    /* ---- SETB C (0xD3) ---- */
    case 0xD3:
        tcg_gen_ori_i32(cpu_psw, cpu_psw, PSW_CY);
        break;

    /* ---- CPL A (0xF4) ---- */
    case 0xF4:
        tcg_gen_xori_i32(cpu_acc, cpu_acc, 0xFF);
        break;

    /* ---- SWAP A (0xC4) ---- */
    case 0xC4: {
        TCGv lo_nib = tcg_temp_new_i32();
        tcg_gen_andi_i32(lo_nib, cpu_acc, 0x0F);
        tcg_gen_shri_i32(cpu_acc, cpu_acc, 4);
        tcg_gen_shli_i32(lo_nib, lo_nib, 4);
        tcg_gen_or_i32(cpu_acc, cpu_acc, lo_nib);
        tcg_temp_free_i32(lo_nib);
        break;
    }

    /* ---- PUSH direct (0xC0) ---- */
    case 0xC0: {
        uint8_t addr = ldub_code(dc->pc++);
        gen_direct_ld8(tmp, addr);
        tcg_gen_addi_i32(cpu_sp, cpu_sp, 1);
        tcg_gen_andi_i32(cpu_sp, cpu_sp, 0xFF);
        gen_iram_st8(cpu_sp, tmp);
        break;
    }

    /* ---- POP direct (0xD0) ---- */
    case 0xD0: {
        uint8_t addr = ldub_code(dc->pc++);
        gen_iram_ld8(tmp, cpu_sp);
        tcg_gen_subi_i32(cpu_sp, cpu_sp, 1);
        tcg_gen_andi_i32(cpu_sp, cpu_sp, 0xFF);
        gen_direct_st8(addr, tmp);
        break;
    }

    /* ---- SJMP rel (0x80) ---- */
    case 0x80: {
        int8_t rel = (int8_t)ldub_code(dc->pc++);
        tcg_gen_movi_i32(cpu_pc, dc->pc + rel);
        break;
    }

    /* ---- AJMP (0x01/0x21/0x41/0x61/0x81/0xA1/0xC1/0xE1) ---- */
    case 0x01: case 0x21: case 0x41: case 0x61:
    case 0x81: case 0xA1: case 0xC1: case 0xE1: {
        uint8_t lo = ldub_code(dc->pc++);
        uint16_t target = (dc->pc & 0xF800) | ((uint16_t)(op & 0xE0) << 3) | lo;
        tcg_gen_movi_i32(cpu_pc, target);
        break;
    }

    /* ---- ACALL (0x11/0x31/0x51/0x71/0x91/0xB1/0xD1/0xF1) ---- */
    case 0x11: case 0x31: case 0x51: case 0x71:
    case 0x91: case 0xB1: case 0xD1: case 0xF1: {
        uint8_t lo = ldub_code(dc->pc++);
        uint16_t target = (dc->pc & 0xF800) | ((uint16_t)(op & 0xE0) << 3) | lo;
        /* Push return */
        TCGv retl = tcg_const_i32(dc->pc & 0xFF);
        TCGv reth = tcg_const_i32((dc->pc >> 8) & 0xFF);
        tcg_gen_addi_i32(cpu_sp, cpu_sp, 1);
        tcg_gen_andi_i32(cpu_sp, cpu_sp, 0xFF);
        gen_iram_st8(cpu_sp, retl);
        tcg_gen_addi_i32(cpu_sp, cpu_sp, 1);
        tcg_gen_andi_i32(cpu_sp, cpu_sp, 0xFF);
        gen_iram_st8(cpu_sp, reth);
        tcg_gen_movi_i32(cpu_pc, target);
        tcg_temp_free_i32(retl);
        tcg_temp_free_i32(reth);
        break;
    }

    /* ---- JZ rel (0x60) ---- */
    case 0x60: {
        int8_t rel = (int8_t)ldub_code(dc->pc++);
        int lend = gen_new_label();
        tcg_gen_brcondi_i32(TCG_COND_NE, cpu_acc, 0, lend);
        tcg_gen_movi_i32(cpu_pc, dc->pc + rel);
        gen_set_label(lend);
        break;
    }

    /* ---- JNZ rel (0x70) ---- */
    case 0x70: {
        int8_t rel = (int8_t)ldub_code(dc->pc++);
        int lend = gen_new_label();
        tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_acc, 0, lend);
        tcg_gen_movi_i32(cpu_pc, dc->pc + rel);
        gen_set_label(lend);
        break;
    }

    /* ---- JC rel (0x40) ---- */
    case 0x40: {
        int8_t rel = (int8_t)ldub_code(dc->pc++);
        tcg_gen_shri_i32(tmp, cpu_psw, 7);
        tcg_gen_andi_i32(tmp, tmp, 1);
        int lend = gen_new_label();
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, lend);
        tcg_gen_movi_i32(cpu_pc, dc->pc + rel);
        gen_set_label(lend);
        break;
    }

    /* ---- JNC rel (0x50) ---- */
    case 0x50: {
        int8_t rel = (int8_t)ldub_code(dc->pc++);
        tcg_gen_shri_i32(tmp, cpu_psw, 7);
        tcg_gen_andi_i32(tmp, tmp, 1);
        int lend = gen_new_label();
        tcg_gen_brcondi_i32(TCG_COND_NE, tmp, 0, lend);
        tcg_gen_movi_i32(cpu_pc, dc->pc + rel);
        gen_set_label(lend);
        break;
    }

    /* ---- DJNZ Rn,rel (0xD8-0xDF) ---- */
    case 0xD8: case 0xD9: case 0xDA: case 0xDB:
    case 0xDC: case 0xDD: case 0xDE: case 0xDF: {
        int rn = op & 7;
        int8_t rel = (int8_t)ldub_code(dc->pc++);
        TCGv bank = tcg_temp_new_i32();
        tcg_gen_shri_i32(bank, cpu_psw, 3);
        tcg_gen_andi_i32(bank, bank, 0x18);
        tcg_gen_addi_i32(bank, bank, rn);
        gen_iram_ld8(tmp, bank);
        tcg_gen_subi_i32(tmp, tmp, 1);
        tcg_gen_andi_i32(tmp, tmp, 0xFF);
        gen_iram_st8(bank, tmp);
        int lend = gen_new_label();
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, lend);
        tcg_gen_movi_i32(cpu_pc, dc->pc + rel);
        gen_set_label(lend);
        tcg_temp_free_i32(bank);
        break;
    }

    /* ---- MOVX A,@DPTR (0xE0) ---- */
    case 0xE0: {
        TCGv addr = tcg_temp_new_i32();
        tcg_gen_shli_i32(addr, cpu_dph, 8);
        tcg_gen_or_i32(addr, addr, cpu_dpl);
        tcg_gen_addi_i32(addr, addr, MCS51_XDATA_BASE);
        tcg_gen_qemu_ld8u(cpu_acc, addr, 0);
        tcg_temp_free_i32(addr);
        break;
    }

    /* ---- MOVX @DPTR,A (0xF0) ---- */
    case 0xF0: {
        TCGv addr = tcg_temp_new_i32();
        tcg_gen_shli_i32(addr, cpu_dph, 8);
        tcg_gen_or_i32(addr, addr, cpu_dpl);
        tcg_gen_addi_i32(addr, addr, MCS51_XDATA_BASE);
        tcg_gen_qemu_st8(cpu_acc, addr, 0);
        tcg_temp_free_i32(addr);
        break;
    }

    /* ---- INC DPTR (0xA3) ---- */
    case 0xA3: {
        TCGv dptr = tcg_temp_new_i32();
        tcg_gen_shli_i32(dptr, cpu_dph, 8);
        tcg_gen_or_i32(dptr, dptr, cpu_dpl);
        tcg_gen_addi_i32(dptr, dptr, 1);
        tcg_gen_andi_i32(cpu_dpl, dptr, 0xFF);
        tcg_gen_shri_i32(cpu_dph, dptr, 8);
        tcg_gen_andi_i32(cpu_dph, cpu_dph, 0xFF);
        tcg_temp_free_i32(dptr);
        break;
    }

    /* ---- Unknown ---- */
    default: {
        TCGv_i32 arg = tcg_const_i32(EXCP_UNDEF);
        gen_helper_raise_exception(cpu_env, arg);
        tcg_temp_free_i32(arg);
        break;
    }

    } /* end switch */

    tcg_temp_free_i32(tmp);
    tcg_temp_free_i32(tmp2);
    return 0;
}

/* ---- New-style TCG hooks ---- */

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
    dc->pc = base->pc;

    tcg_gen_insn_start(base->pc);

    disas_mcs51_insn(env, dc);

    int consumed = dc->pc - base->pc;
    base->pc = dc->pc;
    base->tb->size += consumed;

    if((base->pc - base->tb->pc) != base->tb->size) return 0;
    if((base->pc & (TARGET_PAGE_SIZE - 1)) == 0) return 0;
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
    }
    return 0;
}

void cpu_exec_prologue(CPUState *env) { (void)env; }
void cpu_exec_epilogue(CPUState *env) { (void)env; }

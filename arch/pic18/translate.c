/*
 * PIC18 TCG instruction translator.
 *
 * PIC18 instruction set: mostly 16-bit words; some 2-word (32-bit) instructions.
 * Data space flat model: PIC18_DATA_BASE + (BSR<<8 | f) for banked access,
 * PIC18_DATA_BASE + f for access bank (f < 0x60 → bank 0, f >= 0x60 → SFR).
 *
 * Selected instructions implemented:
 *   Byte-oriented: MOVWF, MOVF, ADDWF, SUBWF, ANDWF, IORWF, XORWF,
 *                  INCF, DECF, CLRF, SETF, NEGF, COMF, RLCF, RRCF
 *   Literal:       MOVLW, ADDLW, SUBLW, ANDLW, IORLW, XORLW, MULLW
 *   Bit:           BSF, BCF, BTFSC, BTFSS, BTG
 *   Control:       GOTO, CALL, RETURN, RETLW, RETFIE, BRA, BC/BNC/BZ/BNZ/bn
 *   Special:       NOP, SLEEP, CLRWDT, TBLRD*, TBLWT*, PUSH, POP
 */
#include "cpu.h"
#include "arch_callbacks.h"
#include "tb-helper.h"
#include "debug.h"

static TCGv cpu_wreg;
static TCGv cpu_bsr;
static TCGv cpu_status;
static TCGv cpu_pc;
static TCGv cpu_fsr0;
static TCGv cpu_fsr1;
static TCGv cpu_fsr2;
static TCGv cpu_prodh;
static TCGv cpu_prodl;
static TCGv cpu_intcon;

void translate_init(void)
{
#define OFFSET(x) offsetof(CPUState, x)
    cpu_wreg   = tcg_global_mem_new_i32(TCG_AREG0, OFFSET(wreg),   "WREG");
    cpu_bsr    = tcg_global_mem_new_i32(TCG_AREG0, OFFSET(bsr),    "BSR");
    cpu_status = tcg_global_mem_new_i32(TCG_AREG0, OFFSET(status), "STATUS");
    cpu_pc     = tcg_global_mem_new_i32(TCG_AREG0, OFFSET(pc),     "PC");
    cpu_fsr0   = tcg_global_mem_new_i32(TCG_AREG0, OFFSET(fsr0),   "FSR0");
    cpu_fsr1   = tcg_global_mem_new_i32(TCG_AREG0, OFFSET(fsr1),   "FSR1");
    cpu_fsr2   = tcg_global_mem_new_i32(TCG_AREG0, OFFSET(fsr2),   "FSR2");
    cpu_prodh  = tcg_global_mem_new_i32(TCG_AREG0, OFFSET(prodh),  "PRODH");
    cpu_prodl  = tcg_global_mem_new_i32(TCG_AREG0, OFFSET(prodl),  "PRODL");
    cpu_intcon = tcg_global_mem_new_i32(TCG_AREG0, OFFSET(intcon), "INTCON");
#undef OFFSET
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

/* Compute effective data address: access bank vs. banked */
static TCGv gen_data_addr(uint8_t f, uint8_t a)
{
    TCGv addr = tcg_temp_new_i32();
    if(a == 1) {
        /* Access bank: f < 0x60 → bank 0, else SFR (top of memory) */
        uint32_t eff = (f < 0x60) ? (PIC18_DATA_BASE + f)
                                  : (PIC18_DATA_BASE + 0xF00 + f - 0x60);
        tcg_gen_movi_i32(addr, eff);
    } else {
        /* Banked: BSR<<8 | f */
        tcg_gen_shli_i32(addr, cpu_bsr, 8);
        tcg_gen_ori_i32(addr, addr, f);
        tcg_gen_addi_i32(addr, addr, PIC18_DATA_BASE);
    }
    return addr;
}

static void gen_load_f(TCGv dst, uint8_t f, uint8_t a)
{
    TCGv addr = gen_data_addr(f, a);
    tcg_gen_qemu_ld8u(dst, addr, 0);
    tcg_temp_free_i32(addr);
}

static void gen_store_f(uint8_t f, uint8_t a, TCGv src)
{
    TCGv addr = gen_data_addr(f, a);
    tcg_gen_qemu_st8(src, addr, 0);
    tcg_temp_free_i32(addr);
}

static void gen_update_zn(TCGv val)
{
    TCGv tmpz = tcg_temp_new_i32();
    TCGv tmpn = tcg_temp_new_i32();
    tcg_gen_setcondi_i32(TCG_COND_EQ, tmpz, val, 0);
    tcg_gen_deposit_i32(cpu_status, cpu_status, tmpz, 2, 1); /* Z = bit2 */
    tcg_gen_shri_i32(tmpn, val, 7);
    tcg_gen_andi_i32(tmpn, tmpn, 1);
    tcg_gen_deposit_i32(cpu_status, cpu_status, tmpn, 4, 1); /* N = bit4 */
    tcg_temp_free_i32(tmpz);
    tcg_temp_free_i32(tmpn);
}

static int disas_pic18_insn(CPUState *env, DisasContext *dc)
{
    uint32_t pc = dc->pc;
    /* Fetch 16-bit instruction */
    uint16_t insn = lduw_code(pc);
    dc->pc = pc + 2;

    uint8_t f = insn & 0xFF;          /* file reg 8-bit  */
    uint8_t a = (insn >> 8) & 1;      /* access bit      */
    uint8_t d = (insn >> 9) & 1;      /* destination bit */
    uint8_t b = (insn >> 9) & 7;      /* bit select      */
    uint8_t k8 = insn & 0xFF;         /* 8-bit literal   */

    TCGv tmp = tcg_temp_new_i32();
    TCGv res = tcg_temp_new_i32();

    /* ---- NOP / special no-ops ---- */
    if(insn == 0x0000 || insn == 0xFFFF) goto done;

    /* ---- SLEEP ---- */
    if(insn == 0x0003) { gen_helper_sleep(cpu_env); goto done; }

    /* ---- CLRWDT ---- */
    if(insn == 0x0004) { gen_helper_wdt_reset(cpu_env); goto done; }

    /* ---- RETURN (0x0012) ---- */
    if(insn == 0x0012) {
        TCGv_i32 arg = tcg_const_i32(EXCP_RESET);
        gen_helper_raise_exception(cpu_env, arg); tcg_temp_free_i32(arg); goto done;
    }

    /* ---- RETFIE fast=0 (0x0010), fast=1 (0x0011) ---- */
    if((insn & 0xFFFE) == 0x0010) {
        tcg_gen_ori_i32(cpu_intcon, cpu_intcon, INTCON_GIEH | INTCON_GIEL);
        TCGv_i32 arg = tcg_const_i32(EXCP_RESET);
        gen_helper_raise_exception(cpu_env, arg); tcg_temp_free_i32(arg); goto done;
    }

    /* ---- MOVLW k (0x0Exx) ---- */
    if((insn & 0xFF00) == 0x0E00) {
        tcg_gen_movi_i32(cpu_wreg, k8); goto done;
    }

    /* ---- ADDLW k (0x0F00) ---- */
    if((insn & 0xFF00) == 0x0F00) {
        tcg_gen_addi_i32(res, cpu_wreg, k8);
        gen_update_zn(res); tcg_gen_andi_i32(cpu_wreg, res, 0xFF); goto done;
    }

    /* ---- SUBLW k (0x0800) ---- */
    if((insn & 0xFF00) == 0x0800) {
        tcg_gen_movi_i32(tmp, k8);
        tcg_gen_sub_i32(res, tmp, cpu_wreg);
        gen_update_zn(res); tcg_gen_andi_i32(cpu_wreg, res, 0xFF); goto done;
    }

    /* ---- ANDLW k (0x0B00) ---- */
    if((insn & 0xFF00) == 0x0B00) {
        tcg_gen_andi_i32(cpu_wreg, cpu_wreg, k8); gen_update_zn(cpu_wreg); goto done;
    }

    /* ---- IORLW k (0x0900) ---- */
    if((insn & 0xFF00) == 0x0900) {
        tcg_gen_ori_i32(cpu_wreg, cpu_wreg, k8); gen_update_zn(cpu_wreg); goto done;
    }

    /* ---- XORLW k (0x0A00) ---- */
    if((insn & 0xFF00) == 0x0A00) {
        tcg_gen_xori_i32(cpu_wreg, cpu_wreg, k8); gen_update_zn(cpu_wreg); goto done;
    }

    /* ---- MULLW k (0x0D00) ---- */
    if((insn & 0xFF00) == 0x0D00) {
        TCGv_i32 kv = tcg_const_i32(k8);
        tcg_gen_mul_i32(res, cpu_wreg, kv); tcg_temp_free_i32(kv);
        tcg_gen_andi_i32(cpu_prodl, res, 0xFF);
        tcg_gen_shri_i32(cpu_prodh, res, 8);
        tcg_gen_andi_i32(cpu_prodh, cpu_prodh, 0xFF);
        goto done;
    }

    /* ---- MOVWF f,a (0x6Exx) ---- */
    if((insn & 0xFE00) == 0x6E00) {
        gen_store_f(f, a, cpu_wreg); goto done;
    }

    /* ---- MOVF f,d,a (0x50xx-53xx) ---- */
    if((insn & 0xFC00) == 0x5000) {
        gen_load_f(tmp, f, a);
        gen_update_zn(tmp);
        if(d == 0) tcg_gen_mov_i32(cpu_wreg, tmp);
        else        gen_store_f(f, a, tmp);
        goto done;
    }

    /* ---- ADDWF f,d,a (0x24xx-27xx) ---- */
    if((insn & 0xFC00) == 0x2400) {
        gen_load_f(tmp, f, a);
        tcg_gen_add_i32(res, cpu_wreg, tmp);
        gen_update_zn(res); tcg_gen_andi_i32(res, res, 0xFF);
        if(d == 0) tcg_gen_mov_i32(cpu_wreg, res); else gen_store_f(f, a, res);
        goto done;
    }

    /* ---- SUBWF f,d,a (0x54xx-57xx) ---- */
    if((insn & 0xFC00) == 0x5400) {
        gen_load_f(tmp, f, a);
        tcg_gen_sub_i32(res, tmp, cpu_wreg);
        gen_update_zn(res); tcg_gen_andi_i32(res, res, 0xFF);
        if(d == 0) tcg_gen_mov_i32(cpu_wreg, res); else gen_store_f(f, a, res);
        goto done;
    }

    /* ---- ANDWF f,d,a (0x14xx-17xx) ---- */
    if((insn & 0xFC00) == 0x1400) {
        gen_load_f(tmp, f, a);
        tcg_gen_and_i32(res, cpu_wreg, tmp);
        gen_update_zn(res);
        if(d == 0) tcg_gen_mov_i32(cpu_wreg, res); else gen_store_f(f, a, res);
        goto done;
    }

    /* ---- IORWF f,d,a (0x10xx-13xx) ---- */
    if((insn & 0xFC00) == 0x1000) {
        gen_load_f(tmp, f, a);
        tcg_gen_or_i32(res, cpu_wreg, tmp);
        gen_update_zn(res);
        if(d == 0) tcg_gen_mov_i32(cpu_wreg, res); else gen_store_f(f, a, res);
        goto done;
    }

    /* ---- XORWF f,d,a (0x18xx-1Bxx) ---- */
    if((insn & 0xFC00) == 0x1800) {
        gen_load_f(tmp, f, a);
        tcg_gen_xor_i32(res, cpu_wreg, tmp);
        gen_update_zn(res);
        if(d == 0) tcg_gen_mov_i32(cpu_wreg, res); else gen_store_f(f, a, res);
        goto done;
    }

    /* ---- INCF f,d,a (0x28xx-2Bxx) ---- */
    if((insn & 0xFC00) == 0x2800) {
        gen_load_f(tmp, f, a);
        tcg_gen_addi_i32(res, tmp, 1); tcg_gen_andi_i32(res, res, 0xFF);
        gen_update_zn(res);
        if(d == 0) tcg_gen_mov_i32(cpu_wreg, res); else gen_store_f(f, a, res);
        goto done;
    }

    /* ---- DECF f,d,a (0x04xx-07xx) ---- */
    if((insn & 0xFC00) == 0x0400) {
        gen_load_f(tmp, f, a);
        tcg_gen_subi_i32(res, tmp, 1); tcg_gen_andi_i32(res, res, 0xFF);
        gen_update_zn(res);
        if(d == 0) tcg_gen_mov_i32(cpu_wreg, res); else gen_store_f(f, a, res);
        goto done;
    }

    /* ---- CLRF f,a (0x6Axx) ---- */
    if((insn & 0xFE00) == 0x6A00) {
        TCGv zero = tcg_const_i32(0);
        gen_store_f(f, a, zero); tcg_temp_free_i32(zero);
        tcg_gen_ori_i32(cpu_status, cpu_status, STATUS_Z);
        goto done;
    }

    /* ---- SETF f,a (0x68xx) ---- */
    if((insn & 0xFE00) == 0x6800) {
        TCGv ff = tcg_const_i32(0xFF);
        gen_store_f(f, a, ff); tcg_temp_free_i32(ff);
        goto done;
    }

    /* ---- NEGF f,a (0x6Cxx) ---- */
    if((insn & 0xFE00) == 0x6C00) {
        gen_load_f(tmp, f, a);
        tcg_gen_neg_i32(res, tmp); tcg_gen_andi_i32(res, res, 0xFF);
        gen_update_zn(res); gen_store_f(f, a, res);
        goto done;
    }

    /* ---- BSF f,b,a (0x80xx-9Fxx) ---- */
    if((insn & 0xE000) == 0x8000 && ((insn >> 10) & 3) == 0) {
        gen_load_f(tmp, f, a);
        tcg_gen_ori_i32(tmp, tmp, (1u << b));
        gen_store_f(f, a, tmp); goto done;
    }

    /* ---- BCF f,b,a (0x90xx) ---- */
    if((insn & 0xE000) == 0x9000 && ((insn >> 10) & 3) == 0) {
        gen_load_f(tmp, f, a);
        tcg_gen_andi_i32(tmp, tmp, ~(1u << b));
        gen_store_f(f, a, tmp); goto done;
    }

    /* ---- BTFSC f,b,a (0xB0xx) ---- */
    if((insn & 0xE000) == 0xB000 && ((insn >> 10) & 3) == 0) {
        gen_load_f(tmp, f, a);
        tcg_gen_andi_i32(tmp, tmp, (1u << b));
        {
            int lend = gen_new_label();
            tcg_gen_brcondi_i32(TCG_COND_NE, tmp, 0, lend);
            tcg_gen_movi_i32(cpu_pc, dc->pc + 2); /* skip */
            gen_set_label(lend);
        }
        goto done;
    }

    /* ---- BTFSS f,b,a (0xA0xx) ---- */
    if((insn & 0xE000) == 0xA000 && ((insn >> 10) & 3) == 0) {
        gen_load_f(tmp, f, a);
        tcg_gen_andi_i32(tmp, tmp, (1u << b));
        {
            int lend = gen_new_label();
            tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, lend);
            tcg_gen_movi_i32(cpu_pc, dc->pc + 2); /* skip */
            gen_set_label(lend);
        }
        goto done;
    }

    /* ---- BRA n (0xD000+n) — unconditional relative branch ---- */
    if((insn & 0xF800) == 0xD000) {
        int16_t offset = (int16_t)((insn & 0x7FF) << 5) >> 5; /* sign-extend 11-bit */
        uint32_t target = dc->pc + (int32_t)offset * 2;
        tcg_gen_movi_i32(cpu_pc, target); goto done;
    }

    /* ---- BZ/BNZ/BC/BNC/BOV etc. (0xE0xx-E7xx) ---- */
    if((insn & 0xF000) == 0xE000) {
        uint8_t cond = (insn >> 8) & 0x07;
        int8_t  off8 = (int8_t)(insn & 0xFF);
        uint32_t target = dc->pc + (int32_t)off8 * 2;
        /* cond: 0=BZ, 1=BNZ, 2=BC, 3=BNC, 4=BOV, 5=BNOV, 6=BN, 7=BNN */
        static const uint8_t status_bits[8] = {2, 2, 0, 0, 3, 3, 4, 4};
        int inv[8] = {0,1,0,1,0,1,0,1};
        TCGv bit = tcg_temp_new_i32();
        tcg_gen_shri_i32(bit, cpu_status, status_bits[cond]);
        tcg_gen_andi_i32(bit, bit, 1);
        {
            int lend = gen_new_label();
            TCGCond cc = inv[cond] ? TCG_COND_NE : TCG_COND_EQ;
            tcg_gen_brcondi_i32(cc, bit, 1, lend);
            tcg_gen_movi_i32(cpu_pc, target);
            gen_set_label(lend);
        }
        tcg_temp_free_i32(bit);
        goto done;
    }

    /* ---- GOTO k (2-word 0xEF00 / 0xF000) ---- */
    if((insn & 0xFF00) == 0xEF00) {
        uint16_t insn2 = lduw_code(dc->pc); dc->pc += 2;
        uint32_t target = ((uint32_t)(insn2 & 0x0FFF) << 8) | (insn & 0xFF);
        target <<= 1; /* word → byte */
        tcg_gen_movi_i32(cpu_pc, target); goto done;
    }

    /* ---- CALL k (2-word 0xEC00 / 0xF000) ---- */
    if((insn & 0xFE00) == 0xEC00) {
        uint16_t insn2 = lduw_code(dc->pc); dc->pc += 2;
        uint32_t target = ((uint32_t)(insn2 & 0x0FFF) << 8) | (insn & 0xFF);
        target <<= 1;
        tcg_gen_movi_i32(cpu_pc, target); goto done;
    }

    /* ---- RETLW k (0x0C00) ---- */
    if((insn & 0xFF00) == 0x0C00) {
        tcg_gen_movi_i32(cpu_wreg, k8);
        TCGv_i32 arg = tcg_const_i32(EXCP_RESET);
        gen_helper_raise_exception(cpu_env, arg); tcg_temp_free_i32(arg); goto done;
    }

    /* Unknown */
    {
        TCGv_i32 arg = tcg_const_i32(EXCP_UNDEF);
        gen_helper_raise_exception(cpu_env, arg); tcg_temp_free_i32(arg);
    }

done:
    tcg_temp_free_i32(tmp);
    tcg_temp_free_i32(res);
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
    dc->pc = base->pc; /* byte address */

    tcg_gen_insn_start(base->pc);

    disas_pic18_insn(env, dc);

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

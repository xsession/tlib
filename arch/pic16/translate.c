/*
 * PIC16 TCG instruction translator.
 *
 * PIC16 instruction set (14-bit words):
 *   Opcode layout:
 *     [13:8] opcode field, [7:0] varied (file addr f, bit b, literal k)
 *
 * File register "f" maps to data space: PIC16_DATA_BASE + (current_bank*0x80 + f)
 * Bank is selected by STATUS[RP1:RP0].
 *
 * Only the most common instructions are implemented; unknown opcodes
 * generate a call to raise_exception(EXCP_UNDEF).
 */
#include "cpu.h"
#include "arch_callbacks.h"
#include "tb-helper.h"
#include "debug.h"

/* TCG global temporaries — CPU registers */
static TCGv cpu_w;       /* W (WREG) */
static TCGv cpu_pc;      /* PC  */
static TCGv cpu_status;  /* STATUS */
static TCGv cpu_fsr;     /* FSR */
static TCGv cpu_pclath;  /* PCLATH */
static TCGv cpu_intcon;  /* INTCON */

void translate_init(void)
{
#define OFFSET(x) offsetof(CPUState, x)
    cpu_w       = tcg_global_mem_new_i32(TCG_AREG0, OFFSET(w),       "W");
    cpu_pc      = tcg_global_mem_new_i32(TCG_AREG0, OFFSET(pc),      "PC");
    cpu_status  = tcg_global_mem_new_i32(TCG_AREG0, OFFSET(status),  "STATUS");
    cpu_fsr     = tcg_global_mem_new_i32(TCG_AREG0, OFFSET(fsr),     "FSR");
    cpu_pclath  = tcg_global_mem_new_i32(TCG_AREG0, OFFSET(pclath),  "PCLATH");
    cpu_intcon  = tcg_global_mem_new_i32(TCG_AREG0, OFFSET(intcon),  "INTCON");
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

/* Helper: load a byte from DATA space (flat modelling — bank ignored for now) */
static void gen_load_freg(TCGv dst, uint8_t f)
{
    TCGv addr = tcg_const_i32(PIC16_DATA_BASE + f);
    tcg_gen_qemu_ld8u(dst, addr, 0);
    tcg_temp_free_i32(addr);
}

/* Helper: store a byte to DATA space */
static void gen_store_freg(uint8_t f, TCGv src)
{
    TCGv addr = tcg_const_i32(PIC16_DATA_BASE + f);
    tcg_gen_qemu_st8(src, addr, 0);
    tcg_temp_free_i32(addr);
}

/* Update Z and C flags from result in `val` */
static void gen_update_zc(TCGv val, TCGv carry)
{
    TCGv tmp = tcg_temp_new_i32();
    /* Z flag: bit 2 of STATUS */
    tcg_gen_setcondi_i32(TCG_COND_EQ, tmp, val, 0);
    tcg_gen_deposit_i32(cpu_status, cpu_status, tmp, 2, 1);
    /* C flag: bit 0 */
    if(carry) tcg_gen_deposit_i32(cpu_status, cpu_status, carry, 0, 1);
    tcg_temp_free_i32(tmp);
}

static int disas_pic16_insn(CPUState *env, DisasContext *dc)
{
    uint32_t pc  = dc->pc;
    /* Fetch 14-bit instruction word from program memory (little-endian 16-bit) */
    uint16_t insn = lduw_code(pc * 2) & 0x3FFF;
    dc->pc = pc + 1;

    /* Decode opcode groups */
    uint8_t hi6 = (insn >> 8) & 0x3F;
    uint8_t f   = insn & 0x7F;   /* file register address 0-127 */
    uint8_t d   = (insn >> 7) & 1; /* destination: 0=W, 1=f */
    uint8_t k8  = insn & 0xFF;   /* 8-bit literal */
    uint8_t k11 = insn & 0x7FF;  /* 11-bit goto/call target */
    uint8_t b   = (insn >> 7) & 7; /* bit number */

    TCGv tmp = tcg_temp_new_i32();
    TCGv res = tcg_temp_new_i32();

    /* ---------- NOP ---------- */
    if(insn == 0x0000) { goto done; }

    /* ---------- CLRW (0x0100) ---------- */
    if(insn == 0x0100) {
        tcg_gen_movi_i32(cpu_w, 0);
        /* set Z */
        tcg_gen_ori_i32(cpu_status, cpu_status, STATUS_Z);
        goto done;
    }

    /* ---------- RETURN (0x0008) ---------- */
    if(insn == 0x0008) {
        /* Model: PC is restored from hardware stack via helper */
        TCGv_i32 arg = tcg_const_i32(EXCP_RESET);
        gen_helper_raise_exception(cpu_env, arg);
        tcg_temp_free_i32(arg);
        goto done;
    }

    /* ---------- RETFIE (0x0009) ---------- */
    if(insn == 0x0009) {
        /* Re-enable GIE then return */
        tcg_gen_ori_i32(cpu_intcon, cpu_intcon, INTCON_GIE);
        TCGv_i32 arg = tcg_const_i32(EXCP_RESET);
        gen_helper_raise_exception(cpu_env, arg);
        tcg_temp_free_i32(arg);
        goto done;
    }

    /* ---------- SLEEP (0x0063) ---------- */
    if(insn == 0x0063) {
        gen_helper_sleep(cpu_env);
        goto done;
    }

    /* ---------- CLRWDT (0x0064) ---------- */
    if(insn == 0x0064) {
        gen_helper_wdt_reset(cpu_env);
        goto done;
    }

    /* ---------- MOVLW k (0x30xx) — load literal to W ---------- */
    if((insn & 0x3F00) == 0x3000) {
        tcg_gen_movi_i32(cpu_w, k8);
        goto done;
    }

    /* ---------- MOVWF f (0x008x) ---------- */
    if((insn & 0x3F80) == 0x0080) {
        if(f == 2) { /* MOVWF PCL — computed jump */
            TCGv newpc = tcg_temp_new_i32();
            tcg_gen_shli_i32(newpc, cpu_pclath, 8);
            tcg_gen_or_i32(newpc, newpc, cpu_w);
            tcg_gen_andi_i32(newpc, newpc, 0x1FFF);
            tcg_gen_mov_i32(cpu_pc, newpc);
            tcg_temp_free_i32(newpc);
        } else {
            gen_store_freg(f, cpu_w);
        }
        goto done;
    }

    /* ---------- MOVF f,d (0x080x+ ) ---------- */
    if((insn & 0x3C00) == 0x0800) {
        gen_load_freg(tmp, f);
        /* Z flag */
        tcg_gen_setcondi_i32(TCG_COND_EQ, res, tmp, 0);
        tcg_gen_deposit_i32(cpu_status, cpu_status, res, 2, 1);
        if(d == 0) tcg_gen_mov_i32(cpu_w, tmp);
        else        gen_store_freg(f, tmp);
        goto done;
    }

    /* ---------- ADDWF f,d ---------- */
    if((insn & 0x3C00) == 0x0700) {
        gen_load_freg(tmp, f);
        tcg_gen_add_i32(res, cpu_w, tmp);
        gen_update_zc(res, NULL);
        tcg_gen_andi_i32(res, res, 0xFF);
        if(d == 0) tcg_gen_mov_i32(cpu_w, res);
        else        gen_store_freg(f, res);
        goto done;
    }

    /* ---------- SUBWF f,d ---------- */
    if((insn & 0x3C00) == 0x0200) {
        gen_load_freg(tmp, f);
        tcg_gen_sub_i32(res, tmp, cpu_w);
        gen_update_zc(res, NULL);
        tcg_gen_andi_i32(res, res, 0xFF);
        if(d == 0) tcg_gen_mov_i32(cpu_w, res);
        else        gen_store_freg(f, res);
        goto done;
    }

    /* ---------- ANDWF f,d ---------- */
    if((insn & 0x3C00) == 0x0500) {
        gen_load_freg(tmp, f);
        tcg_gen_and_i32(res, cpu_w, tmp);
        gen_update_zc(res, NULL);
        tcg_gen_andi_i32(res, res, 0xFF);
        if(d == 0) tcg_gen_mov_i32(cpu_w, res);
        else        gen_store_freg(f, res);
        goto done;
    }

    /* ---------- IORWF f,d ---------- */
    if((insn & 0x3C00) == 0x0400) {
        gen_load_freg(tmp, f);
        tcg_gen_or_i32(res, cpu_w, tmp);
        gen_update_zc(res, NULL);
        tcg_gen_andi_i32(res, res, 0xFF);
        if(d == 0) tcg_gen_mov_i32(cpu_w, res);
        else        gen_store_freg(f, res);
        goto done;
    }

    /* ---------- XORWF f,d ---------- */
    if((insn & 0x3C00) == 0x0600) {
        gen_load_freg(tmp, f);
        tcg_gen_xor_i32(res, cpu_w, tmp);
        gen_update_zc(res, NULL);
        tcg_gen_andi_i32(res, res, 0xFF);
        if(d == 0) tcg_gen_mov_i32(cpu_w, res);
        else        gen_store_freg(f, res);
        goto done;
    }

    /* ---------- INCF f,d ---------- */
    if((insn & 0x3C00) == 0x0A00) {
        gen_load_freg(tmp, f);
        tcg_gen_addi_i32(res, tmp, 1);
        gen_update_zc(res, NULL);
        tcg_gen_andi_i32(res, res, 0xFF);
        if(d == 0) tcg_gen_mov_i32(cpu_w, res);
        else        gen_store_freg(f, res);
        goto done;
    }

    /* ---------- DECF f,d ---------- */
    if((insn & 0x3C00) == 0x0300) {
        gen_load_freg(tmp, f);
        tcg_gen_subi_i32(res, tmp, 1);
        gen_update_zc(res, NULL);
        tcg_gen_andi_i32(res, res, 0xFF);
        if(d == 0) tcg_gen_mov_i32(cpu_w, res);
        else        gen_store_freg(f, res);
        goto done;
    }

    /* ---------- CLRF f (0x018x) ---------- */
    if((insn & 0x3F80) == 0x0180) {
        TCGv zero = tcg_const_i32(0);
        gen_store_freg(f, zero);
        tcg_temp_free_i32(zero);
        tcg_gen_ori_i32(cpu_status, cpu_status, STATUS_Z);
        goto done;
    }

    /* ---------- ADDLW k (0x3Exx/0x3Fxx) ---------- */
    if((insn & 0x3E00) == 0x3E00) {
        tcg_gen_addi_i32(res, cpu_w, k8);
        gen_update_zc(res, NULL);
        tcg_gen_andi_i32(cpu_w, res, 0xFF);
        goto done;
    }

    /* ---------- SUBLW k (0x3Cxx/0x3Dxx) ---------- */
    if((insn & 0x3E00) == 0x3C00) {
        tcg_gen_movi_i32(tmp, k8);
        tcg_gen_sub_i32(res, tmp, cpu_w);
        gen_update_zc(res, NULL);
        tcg_gen_andi_i32(cpu_w, res, 0xFF);
        goto done;
    }

    /* ---------- ANDLW k (0x3900) ---------- */
    if((insn & 0x3F00) == 0x3900) {
        tcg_gen_andi_i32(cpu_w, cpu_w, k8);
        gen_update_zc(cpu_w, NULL);
        goto done;
    }

    /* ---------- IORLW k (0x3800) ---------- */
    if((insn & 0x3F00) == 0x3800) {
        tcg_gen_ori_i32(cpu_w, cpu_w, k8);
        gen_update_zc(cpu_w, NULL);
        goto done;
    }

    /* ---------- XORLW k (0x3A00/0x3B00) ---------- */
    if((insn & 0x3E00) == 0x3A00) {
        tcg_gen_xori_i32(cpu_w, cpu_w, k8);
        gen_update_zc(cpu_w, NULL);
        goto done;
    }

    /* ---------- RETLW k (0x34xx-37xx) ---------- */
    if((insn & 0x3C00) == 0x3400) {
        tcg_gen_movi_i32(cpu_w, k8);
        TCGv_i32 arg = tcg_const_i32(EXCP_RESET);
        gen_helper_raise_exception(cpu_env, arg);
        tcg_temp_free_i32(arg);
        goto done;
    }

    /* ---------- BSF f,b (0x14xx-17xx) ---------- */
    if((insn & 0x3C00) == 0x1400) {
        gen_load_freg(tmp, f);
        tcg_gen_ori_i32(tmp, tmp, (1u << b));
        gen_store_freg(f, tmp);
        goto done;
    }

    /* ---------- BCF f,b (0x1000-13xx) ---------- */
    if((insn & 0x3C00) == 0x1000) {
        gen_load_freg(tmp, f);
        tcg_gen_andi_i32(tmp, tmp, ~(1u << b));
        gen_store_freg(f, tmp);
        goto done;
    }

    /* ---------- BTFSC f,b (0x1800-1Bxx) — skip if bit clear ---------- */
    if((insn & 0x3C00) == 0x1800) {
        gen_load_freg(tmp, f);
        tcg_gen_andi_i32(tmp, tmp, (1u << b));
        /* If bit == 0 → skip next instruction (PC+2) else fall through (PC+1) */
        {
            int l_skip = gen_new_label();
            int l_end  = gen_new_label();
            tcg_gen_brcondi_i32(TCG_COND_NE, tmp, 0, l_end);
            gen_set_label(l_skip);
            tcg_gen_movi_i32(cpu_pc, dc->pc + 1); /* skip next */
            tcg_gen_br(l_end);
            gen_set_label(l_end);
        }
        goto done;
    }

    /* ---------- BTFSS f,b (0x1C00-1Fxx) — skip if bit set ---------- */
    if((insn & 0x3C00) == 0x1C00) {
        gen_load_freg(tmp, f);
        tcg_gen_andi_i32(tmp, tmp, (1u << b));
        {
            int l_end = gen_new_label();
            tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, l_end);
            tcg_gen_movi_i32(cpu_pc, dc->pc + 1);
            gen_set_label(l_end);
        }
        goto done;
    }

    /* ---------- CALL k (0x2000-27xx) ---------- */
    if((insn & 0x3800) == 0x2000) {
        uint32_t target = ((uint32_t)(env->pclath & 0x18) << 8) | k11;
        /* Push return address (simplified — just set PC) */
        tcg_gen_movi_i32(cpu_pc, target);
        goto done;
    }

    /* ---------- GOTO k (0x2800-2Fff) ---------- */
    if((insn & 0x3800) == 0x2800) {
        uint32_t target = ((uint32_t)(env->pclath & 0x18) << 8) | k11;
        tcg_gen_movi_i32(cpu_pc, target);
        goto done;
    }

    /* Unknown — raise UNDEF */
    {
        TCGv_i32 arg = tcg_const_i32(EXCP_UNDEF);
        gen_helper_raise_exception(cpu_env, arg);
        tcg_temp_free_i32(arg);
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
    dc->pc = base->pc / 2; /* byte address → word address */

    tcg_gen_insn_start(base->pc);

    disas_pic16_insn(env, dc);

    int consumed = (dc->pc - base->pc / 2) * 2; /* word → bytes */
    base->pc += consumed;
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

/*
 * AVR op_helper — exception/interrupt dispatch.
 *
 * Vector table layout (reset vectors at 0x000000):
 *   vector N starts at byte address (N * 4) for 2-word jump instruction.
 *   For ATmega328P: 2 bytes per vector slot (RJMP = 1 word).
 *   For ATmega2560: 4 bytes per vector slot (JMP  = 2 words).
 *
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */
#include "cpu.h"
#include "arch_callbacks.h"
#include "softmmu_exec.h"
#define HELPER_H
#include "helper.h"

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static void raise_exception(CPUState *env, int excp)
{
    env->exception_index = excp;
    cpu_loop_exit(env);
}

/* ------------------------------------------------------------------ */
/* TCG helpers (env is passed explicitly by the framework)            */
/* ------------------------------------------------------------------ */

void HELPER(raise_exception)(CPUState *env, uint32_t excp)
{
    raise_exception(env, (int)excp);
}

void HELPER(sleep)(CPUState *env)
{
    env->wfi = 1;
    env->exception_index = EXCP_INT0;
    cpu_loop_exit(env);
}

void HELPER(wdt_reset)(CPUState *env)
{
    cpu_reset(env);
    cpu_loop_exit(env);
}

void HELPER(check_align)(CPUState *env, target_ulong addr, uint32_t required_align)
{
    if(addr & (required_align - 1)) {
        raise_exception(env, EXCP_UNDEF);
    }
}

/* ------------------------------------------------------------------ */
/* Unaligned access handler (called by tlib core)                     */
/* ------------------------------------------------------------------ */

void do_unaligned_access(CPUState *env, target_ulong vaddr,
                         int access_type, int mmu_idx,
                         uintptr_t retaddr)
{
    raise_exception(env, EXCP_UNDEF);
}

/* ------------------------------------------------------------------ */
/* MMU / TLB helpers (required by framework)                          */
/* ------------------------------------------------------------------ */

void arch_raise_mmu_fault_exception(CPUState *env, int errcode,
                                    int access_type, target_ulong address,
                                    void *retaddr)
{
    env->exception_index = EXCP_UNDEF;
    cpu_loop_exit_restore(env, (uintptr_t)retaddr, access_type != 2);
}

int arch_tlb_fill(CPUState *env, target_ulong addr, int access_type,
                  int mmu_idx, void *retaddr, int no_page_fault,
                  int access_width, target_phys_addr_t *paddr)
{
    return cpu_handle_mmu_fault(env, addr, access_type, mmu_idx,
                                access_width, no_page_fault, paddr);
}

/* ------------------------------------------------------------------ */
/* Interrupt dispatch                                                  */
/* ------------------------------------------------------------------ */

void do_interrupt(CPUState *env)
{
    int excp = env->exception_index;

    if(excp == EXCP_RESET) {
        cpu_reset(env);
        return;
    }

    if(excp >= EXCP_INT_FIRST) {
        /* Clear global interrupt enable */
        env->sreg &= ~SREG_I;

        /*
         * Push PC onto the stack (2 bytes, little-endian).
         * SP points to the next free slot (pre-decrement model).
         */
        uint32_t sp = env->sp;
        stb_data(AVR_DATA_BASE + sp, env->pc & 0xFF);
        sp--;
        stb_data(AVR_DATA_BASE + sp, (env->pc >> 8) & 0xFF);
        sp--;
        env->sp = sp & 0xFFFF;

        /* Jump to interrupt vector: vector base = excp * 4 (2-word entries) */
        env->pc = (uint32_t)(excp * 4);

        tlib_acknowledge_interrupt(excp);
    }
}

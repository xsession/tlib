/*
 * dsPIC33 op_helper — TCG helper function implementations.
 *
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */
#include "cpu.h"
#include "arch_callbacks.h"
#include "softmmu_exec.h"
#define HELPER_H
#include "helper.h"

static void raise_exception(CPUState *env, uint32_t trap)
{
    env->exception_index = trap;
    cpu_loop_exit(env);
}

void HELPER(raise_exception)(CPUState *env, uint32_t trap)
{
    raise_exception(env, trap);
}

void HELPER(power_down)(CPUState *env)
{
    tlib_on_cpu_power_down();
    env->wfi = 1;
    cpu_loop_exit(env);
}

void HELPER(wait_for_interrupt)(CPUState *env)
{
    tlib_on_cpu_halted();
    env->wfi = 1;
    cpu_loop_exit(env);
}

void HELPER(check_align)(CPUState *env, target_ulong addr, uint32_t align)
{
    if(addr & align) {
        raise_exception(env, EXCP_ADDR_ERROR);
    }
}

void do_unaligned_access(CPUState *env, target_ulong vaddr,
                         int access_type, int mmu_idx, uintptr_t retaddr)
{
    raise_exception(env, EXCP_ADDR_ERROR);
}

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

void do_interrupt(CPUState *cpu_state)
{
    int vector = cpu_state->exception_index;

    if(vector >= EXCP_INT_FIRST) {
        /* External interrupt — push PC, jump to IVT entry               */
        uint32_t ivt_base = 0x000004;            /* IVT: 0x000004 onward  */
        uint32_t vector_addr = ivt_base + (uint32_t)(vector - EXCP_INT_FIRST) * 4;

        /* Push the return address onto the hardware stack (W15) */
        uint32_t sp = cpu_state->w[15];
        sp -= 4;
        stl_phys(sp, cpu_state->pc);
        cpu_state->w[15] = sp;

        /* Fetch interrupt handler address from IVT */
        cpu_state->pc = ldl_phys(vector_addr) & 0x00FFFFFEu;

        /* Acknowledge the interrupt source */
        tlib_acknowledge_interrupt(vector - EXCP_INT_FIRST);
    } else {
        /* Hardware trap — jump to trap vector */
        uint32_t tvt_base  = 0x000014;           /* Trap Vector Table     */
        uint32_t trap_addr = tvt_base + (uint32_t)vector * 4;

        uint32_t sp = cpu_state->w[15];
        sp -= 4;
        stl_phys(sp, cpu_state->pc);
        cpu_state->w[15] = sp;

        cpu_state->pc = ldl_phys(trap_addr) & 0x00FFFFFEu;
    }
}

/*
 * TMS320C28x (C2000) op_helper implementations.
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

void HELPER(idle)(CPUState *env)
{
    tlib_on_cpu_halted();
    env->wfi = 1;
    cpu_loop_exit(env);
}

void HELPER(check_align)(CPUState *env, target_ulong addr, uint32_t align)
{
    if(addr & align) {
        raise_exception(env, EXCP_UNDEF);
    }
}

void do_unaligned_access(CPUState *env, target_ulong vaddr,
                         int access_type, int mmu_idx, uintptr_t retaddr)
{
    raise_exception(env, EXCP_UNDEF);
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

    if(vector >= EXCP_PIE_INT) {
        /* PIE multiplexed interrupt — the PIE controller provides the ISR address */
        /* Save return context: push PC, push ST0/ST1 */
        uint32_t sp = cpu_state->sp;

        /* C2000 stack grows up; SP points to next free word */
        stw_phys((target_phys_addr_t)(sp * 2),       (uint16_t)(cpu_state->st0));
        stw_phys((target_phys_addr_t)((sp + 1) * 2), (uint16_t)(cpu_state->st1));
        stw_phys((target_phys_addr_t)((sp + 2) * 2), (uint16_t)(cpu_state->pc & 0xFFFF));
        stw_phys((target_phys_addr_t)((sp + 3) * 2), (uint16_t)((cpu_state->pc >> 16) & 0x3F));
        cpu_state->sp = sp + 4;

        /* Fetch vector from PIE vector table (based at 0xD00 in C28x word addresses) */
        uint32_t pie_base = 0xD00u;
        uint32_t vec_idx  = (uint32_t)(vector - EXCP_PIE_INT);
        uint32_t vec_addr = pie_base + vec_idx * 2;
        uint32_t lo = lduw_phys((target_phys_addr_t)(vec_addr * 2));
        uint32_t hi = lduw_phys((target_phys_addr_t)((vec_addr + 1) * 2));
        cpu_state->pc = (lo | (hi << 16)) * 2;   /* byte address */

        tlib_acknowledge_interrupt(vec_idx);
    } else if(vector >= EXCP_INT1 && vector <= EXCP_INT14) {
        /* CPU-level interrupt (INT1–INT14).  Vector table at 0x3F FFxx */
        uint32_t cpu_vect_base = 0x3FFFC0u;      /* byte address */
        uint32_t vec_idx       = (uint32_t)(vector - EXCP_INT1);
        uint32_t vec_addr      = cpu_vect_base + vec_idx * 4;

        uint32_t sp = cpu_state->sp;
        stw_phys((target_phys_addr_t)(sp * 2),       (uint16_t)(cpu_state->pc & 0xFFFF));
        stw_phys((target_phys_addr_t)((sp + 1) * 2), (uint16_t)((cpu_state->pc >> 16) & 0x3F));
        cpu_state->sp = sp + 2;

        uint32_t lo = lduw_phys((target_phys_addr_t)vec_addr);
        uint32_t hi = lduw_phys((target_phys_addr_t)(vec_addr + 2));
        cpu_state->pc = (lo | (hi << 16)) * 2;

        tlib_acknowledge_interrupt(vector);
    } else {
        /* Reset */
        cpu_state->pc = 0x3FFFC0u * 2;
    }
}

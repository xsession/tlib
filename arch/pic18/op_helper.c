#include "cpu.h"
#include "arch_callbacks.h"
#include "softmmu_exec.h"
#include <stdint.h>
#define HELPER_H
#include "helper.h"

static void raise_exception(CPUState *env, int excp)
{
    env->exception_index = excp;
    cpu_loop_exit(env);
}

void HELPER(raise_exception)(CPUState *env, uint32_t excp)
{
    raise_exception(env, (int)excp);
}

void HELPER(sleep)(CPUState *env)
{
    env->wfi = 1;
    tlib_on_cpu_halted();
    cpu_loop_exit(env);
}

void HELPER(wdt_reset)(CPUState *env)
{
    cpu_reset(env);
    cpu_loop_exit(env);
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

/* Write a 32-bit value to data memory (guest store through the MMU). */
static void stk_addr_write(CPUState *env, uint32_t addr, uint32_t value)
{
    stl_data(addr, value);
}

/* Push return address onto hardware stack, then jump to vector. */
static void push_pc(CPUState *env, uint32_t ret_addr)
{
    uint32_t stk_addr = PIC18_DATA_BASE + 0xFFD; /* TOS area */
    env->stkptr = (env->stkptr + 1) & 0x1F;
    stk_addr_write(env, stk_addr, ret_addr); /* simplified: rely on memory model */
    env->tos = ret_addr;
}

void do_interrupt(CPUState *env)
{
    uint32_t ret = env->pc;
    int high_prio = (env->intcon & INTCON_GIEH) &&
                    (env->exception_index == EXCP_INT_HIGH);

    if(high_prio)
    {
        env->intcon &= ~INTCON_GIEH;
        env->pc = 0x000008; /* high-priority ISR */
    }
    else
    {
        env->intcon &= ~INTCON_GIEL;
        env->pc = 0x000018; /* low-priority ISR */
    }
    env->exception_index = -1;
}

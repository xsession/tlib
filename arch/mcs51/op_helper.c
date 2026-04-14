#include "cpu.h"
#include "arch_callbacks.h"
#include "softmmu_exec.h"
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

void HELPER(power_down)(CPUState *env)
{
    env->wfi = 1;
    tlib_on_cpu_power_down();
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

/*
 * 8051 interrupt entry:
 *   1. Push PC (low byte first, then high) onto internal stack
 *   2. Clear the corresponding flag (outside tlib, handled by C#)
 *   3. Jump to vector = 0x0003 + (n-1)*8
 */
void do_interrupt(CPUState *env)
{
    uint32_t sp = env->sp;

    /* Push PCL */
    sp++;
    stb_phys(MCS51_IRAM_BASE + (sp & 0xFF), env->pc & 0xFF);
    /* Push PCH */
    sp++;
    stb_phys(MCS51_IRAM_BASE + (sp & 0xFF), (env->pc >> 8) & 0xFF);

    env->sp = sp & 0xFF;
    env->pc = MCS51_VECTOR_ADDR(env->exception_index);
    env->exception_index = -1;
}

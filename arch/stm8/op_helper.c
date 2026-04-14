/*
 * STM8 op_helper — exception/interrupt dispatch.
 *
 * STM8 vector table (each entry = 4 bytes: 0x82 XX YY ZZ = JP longmem):
 *   0x8000  RESET
 *   0x8004  TRAP
 *   0x8008  NMI (TLI)
 *   0x800C  IRQ0
 *   0x8010  IRQ1
 *   …
 *
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */
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

void HELPER(halt)(CPUState *env)
{
    env->wfi = 1;
    tlib_on_cpu_power_down();
    cpu_loop_exit(env);
}

void HELPER(wfi)(CPUState *env)
{
    env->wfi = 1;
    env->exception_index = EXCP_INT_FIRST;
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

void do_interrupt(CPUState *env)
{
    int excp = env->exception_index;

    if(excp == EXCP_RESET) {
        cpu_reset(env);
        return;
    }

    /*
     * Push CC, A, X (XH, XL), Y (YH, YL), PC (PCE, PCH, PCL) — 9 bytes total.
     * STM8 stack grows downward; SP points to last used byte.
     */
    uint32_t sp = env->sp;

#define PUSH8(val) do { stb_data(sp, (val) & 0xFF); sp--; } while(0)

    /* Push in order: PCL, PCH, PCE, YL, YH, XL, XH, A, CC */
    PUSH8(env->pc & 0xFF);
    PUSH8((env->pc >>  8) & 0xFF);
    PUSH8((env->pc >> 16) & 0xFF);
    PUSH8(env->y & 0xFF);
    PUSH8((env->y >> 8) & 0xFF);
    PUSH8(env->x & 0xFF);
    PUSH8((env->x >> 8) & 0xFF);
    PUSH8(env->a);
    PUSH8(env->cc);
#undef PUSH8

    env->sp = sp & 0xFFFF;

    /* Mask all interrupts while servicing */
    env->cc |= CC_I_MASK;

    if(excp == EXCP_TRAP) {
        env->pc = 0x8004;
    } else if(excp == EXCP_NMI) {
        env->pc = 0x8008;
    } else {
        /* maskable IRQ: vector at 0x800C + (irq_num * 4) */
        int irq_num = excp - EXCP_INT_FIRST;
        env->pc = 0x800C + irq_num * 4;
        tlib_acknowledge_interrupt(irq_num);
    }
}

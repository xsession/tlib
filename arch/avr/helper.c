/*
 * AVR cpu_reset / cpu_init / cpu_handle_mmu_fault.
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */
#include "cpu.h"
#include <string.h>

void cpu_reset(CPUState *env)
{
    memset(env->r, 0, sizeof(env->r));
    env->pc    = 0;
    env->sp    = 0;
    env->sreg  = 0;
    env->rampz = 0;
    env->rampx = 0;
    env->rampy = 0;
    env->eind  = 0;
}

int cpu_init(const char *cpu_model)
{
    cpu_reset(cpu);
    return 0;
}

void tlib_arch_dispose(void)
{
    /* nothing to free */
}

int cpu_handle_mmu_fault(CPUState *env, target_ulong address,
                         int access_type, int mmu_idx,
                         int access_width, int no_page_fault,
                         target_phys_addr_t *paddr)
{
    /* AVR has no MMU — direct identity mapping */
    *paddr = address;
    int prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
    tlb_set_page(env, address & TARGET_PAGE_MASK,
                 address & TARGET_PAGE_MASK,
                 prot, mmu_idx, TARGET_PAGE_SIZE);
    return 0;
}

target_phys_addr_t cpu_get_phys_page_debug(CPUState *env, target_ulong addr)
{
    /* AVR has no MMU — identity mapping */
    return addr;
}

uint64_t cpu_get_state_for_memory_transaction(CPUState *env, target_ulong addr, int access_type)
{
    return 0;
}

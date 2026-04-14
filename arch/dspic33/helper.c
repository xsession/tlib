/*
 * dsPIC33 CPU helpers: reset and init.
 *
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */
#include <string.h>
#include "cpu.h"
#include "infrastructure.h"
#include "arch_callbacks.h"

void cpu_reset(CPUState *env)
{
    memset(env->w,   0, sizeof(env->w));
    env->pc      = 0;
    env->status  = 0;
    env->tblpag  = 0;
    env->psvpag  = 0;
    env->rcount  = 0;
    env->dcount  = 0;
    env->corcon  = 0;
    env->disicnt = 0;
    env->wfi     = 0;
}

int cpu_init(const char *cpu_model)
{
    /* dsPIC33 has multiple device variants (CK, EP, CH, …).
     * All share the same core architecture; ignore the model string. */
    (void)cpu_model;
    cpu_reset(cpu);
    return 0;
}

void tlib_arch_dispose(void)
{
    /* Nothing heap-allocated in the CPUState def. */
}

int cpu_handle_mmu_fault(CPUState *env, target_ulong address,
                         int access_type, int mmu_idx,
                         int access_width, int no_page_fault,
                         target_phys_addr_t *paddr)
{
    *paddr = address;
    int prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
    tlb_set_page(env, address & TARGET_PAGE_MASK,
                 address & TARGET_PAGE_MASK,
                 prot, mmu_idx, TARGET_PAGE_SIZE);
    return 0;
}

target_phys_addr_t cpu_get_phys_page_debug(CPUState *env, target_ulong addr)
{
    return addr;
}

uint64_t cpu_get_state_for_memory_transaction(CPUState *env, target_ulong addr, int access_type)
{
    return 0;
}

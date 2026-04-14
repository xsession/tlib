/*
 * TMS320C28x (C2000) CPU helpers: reset and init.
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
    env->acc    = 0;
    env->p      = 0;
    env->xt     = 0;
    memset(env->xar, 0, sizeof(env->xar));
    env->sp     = 0x0400;  /* default stack at low data RAM */
    env->pc     = 0x3FFFC0 * 2;  /* reset vector (byte address) */
    env->rpc    = 0;
    env->st0    = 0x0000;
    env->st1    = 0x0B00;  /* VMAP=1, M0M1MAP=1 default */
    env->ifr    = 0;
    env->ier    = 0;
    env->dp     = 0;
    env->wfi    = 0;
}

int cpu_init(const char *cpu_model)
{
    (void)cpu_model;
    cpu_reset(cpu);
    return 0;
}

void tlib_arch_dispose(void)
{
    /* Nothing to free. */
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

#include "cpu.h"
#include <string.h>

void cpu_reset(CPUState *env)
{
    env->pc  = 0;
    env->sp  = 0x07;
    env->ie  = 0;
    env->ip  = 0;
    env->acc = 0;
    env->b   = 0;
    env->psw = 0;
    env->dpl = 0;
    env->dph = 0;
}

int cpu_init(const char *cpu_model)
{
    cpu_reset(cpu);
    return 0;
}

void tlib_arch_dispose(void) { }

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

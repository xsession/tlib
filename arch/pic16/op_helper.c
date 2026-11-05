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

void HELPER(sleep)(CPUState *env)
{
    env->wfi = 1;
    tlib_on_cpu_halted();
    cpu_loop_exit(env);
}

void HELPER(wdt_clrwdt)(CPUState *env)
{
    (void)env;
    /* CLRWDT restarts the watchdog; the C# layer owns the WDT timer
     * (PIC12.WdtClrwdt -> LimitTimer restart). */
    tlib_wdt_clrwdt();
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

/* Called by the TCG framework when an interrupt is pending.
 * We save state (just the return address on the 8-level hardware stack)
 * then jump to interrupt vector at 0x0004 (PIC16 interrupt handler). */
void do_interrupt(CPUState *env)
{
    /* Disable GIE to prevent re-entrance */
    env->intcon &= ~INTCON_GIE;

    /* PIC16 single-vector interrupt: push PC, jump to 0x0004 */
    /* The hardware stack is not emulated as memory; model it as a simple
     * backup. The real stack is internal to the core and depth-limited.
     * For tlib we rely on the C# layer to save/restore via GetStackTrace/SetPC. */
    /* Jump to interrupt vector (word address 4 → byte addr 4) */
    env->pc = 4;
    env->exception_index = -1;
}

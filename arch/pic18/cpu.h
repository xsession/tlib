#ifndef PIC18_CPU_H
#define PIC18_CPU_H

#define NB_MMU_MODES    1
#define TARGET_PAGE_BITS 8
#define TARGET_PHYS_ADDR_SPACE_BITS 24

#include "cpu_registers.h"
#include "cpu-defs.h"

/* PIC18 — enhanced 8-bit Harvard MCU.
 * Program memory: 0x000000 - 0x7FFFFF (21-bit byte addresses, 16-bit words)
 * Data space:     emulated at 0xA00000 + offset
 * Interrupt vectors:
 *   High-priority: 0x000008
 *   Low-priority:  0x000018
 */
#define PIC18_PROG_BASE    0x000000u
#define PIC18_DATA_BASE    0xA00000u

/* Exception numbers */
#define EXCP_RESET      0
#define EXCP_INT_HIGH   1   /* high-priority interrupt */
#define EXCP_INT_LOW    2   /* low-priority interrupt  */
#define EXCP_INT_FIRST  1
#define EXCP_UNDEF      63

/* STATUS register bits */
#define STATUS_C    (1u << 0)
#define STATUS_DC   (1u << 1)
#define STATUS_Z    (1u << 2)
#define STATUS_OV   (1u << 3)
#define STATUS_N    (1u << 4)

/* INTCON bits */
#define INTCON_IPEN   (1u << 7)  /* interrupt priority enable */
#define INTCON_GIEH   (1u << 6)  /* global high-prio enable   */
#define INTCON_GIEL   (1u << 5)  /* global low-prio enable    */

typedef struct CPUState {
    CPU_COMMON

    uint32_t wreg;   /* working register       */
    uint32_t bsr;    /* bank select register   */
    uint32_t status; /* STATUS                 */
    uint32_t pc;     /* program counter (21-bit byte address) */
    uint32_t fsr0;   /* FSR0 (16-bit)          */
    uint32_t fsr1;   /* FSR1                   */
    uint32_t fsr2;   /* FSR2                   */
    uint32_t stkptr; /* hardware stack pointer (5-bit, 31-level) */
    uint32_t tos;    /* top-of-stack           */
    uint32_t prodh;  /* PRODH (8-bit)          */
    uint32_t prodl;  /* PRODL (8-bit)          */
    uint32_t tablat; /* TABLAT — table latch   */
    uint32_t tblptr; /* TBLPTR (22-bit)        */
    uint32_t intcon; /* INTCON                 */
} CPUState;

static inline int cpu_mmu_index(CPUState *env)
{
    return 0;
}

static inline int cpu_interrupts_enabled(CPUState *env)
{
    return (env->intcon & (INTCON_GIEH | INTCON_GIEL)) != 0;
}

int cpu_handle_mmu_fault(CPUState *env, target_ulong address,
                         int access_type, int mmu_idx,
                         int access_width, int no_page_fault,
                         target_phys_addr_t *paddr);

#include "cpu-all.h"

typedef struct DisasContext {
    struct DisasContextBase base;
    uint32_t pc;  /* decoder-private PC tracking */
} DisasContext;

#include "exec-all.h"

static inline int cpu_has_work(CPUState *env)
{
    return (env->interrupt_request & CPU_INTERRUPT_HARD) != 0;
}

static inline void cpu_get_tb_cpu_state(CPUState *env,
                                        target_ulong *pc,
                                        target_ulong *cs_base,
                                        int *flags)
{
    *pc      = env->pc;
    *cs_base = 0;
    *flags   = 0;
}

static inline void cpu_pc_from_tb(CPUState *env, TranslationBlock *tb)
{
    env->pc = tb->pc;
}

void do_interrupt(CPUState *env);

#endif /* PIC18_CPU_H */

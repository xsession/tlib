#ifndef PIC16_CPU_H
#define PIC16_CPU_H

#define NB_MMU_MODES    1
#define TARGET_PAGE_BITS 8
#define TARGET_PHYS_ADDR_SPACE_BITS 24

#include "cpu_registers.h"
#include "cpu-defs.h"

/* PIC16 is Harvard, 8-bit data bus, 14-bit instructions (stored as 16-bit words).
 * Program memory word addresses:  0x000000 - 0x001FFF (PIC16F877A: 8K words)
 * Data space (file registers):    emulated at 0x200000 + offset
 * Each instruction is one 14-bit word; the CPU fetches 16-bit aligned words.
 */
#define PIC16_PROG_BASE    0x000000u
#define PIC16_DATA_BASE    0x200000u

/* Exception numbers */
#define EXCP_RESET         0
#define EXCP_INT           1    /* GIE-gated interrupt */
#define EXCP_INT_FIRST     1
#define EXCP_UNDEF         63

/* STATUS register bits */
#define STATUS_C    (1u << 0)
#define STATUS_DC   (1u << 1)
#define STATUS_Z    (1u << 2)
#define STATUS_PD   (1u << 3)
#define STATUS_TO   (1u << 4)
#define STATUS_RP0  (1u << 5)
#define STATUS_RP1  (1u << 6)
#define STATUS_IRP  (1u << 7)

/* INTCON bits */
#define INTCON_RBIF  (1u << 0)
#define INTCON_INTF  (1u << 1)
#define INTCON_T0IF  (1u << 2)
#define INTCON_RBIE  (1u << 3)
#define INTCON_INTE  (1u << 4)
#define INTCON_T0IE  (1u << 5)
#define INTCON_PEIE  (1u << 6)
#define INTCON_GIE   (1u << 7)

typedef struct CPUState {
    CPU_COMMON

    uint32_t w;          /* WREG (working register) */
    uint32_t pc;         /* program counter (word address, 13 bits) */
    uint32_t status;     /* STATUS file register */
    uint32_t fsr;        /* FSR — file select register (indirect addressing) */
    uint32_t pclath;     /* PCLATH — held during computed goto */
    uint32_t intcon;     /* INTCON */
    uint32_t option_reg; /* OPTION_REG */
} CPUState;

static inline int cpu_mmu_index(CPUState *env)
{
    return 0;
}

static inline int cpu_interrupts_enabled(CPUState *env)
{
    return (env->intcon & INTCON_GIE) != 0;
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

#endif /* PIC16_CPU_H */

#pragma once

/*
 * TMS320C28x (C2000) CPU emulation
 *
 * The TMS320C28x is a 32-bit fixed-point DSP processor from Texas Instruments.
 * It is a modified Harvard architecture with unified address space access.
 *
 * Key architectural features:
 *   - ACC  (32-bit accumulator):  AH:AL split view
 *   - P    (32-bit product):      PH:PL split view
 *   - XT   (32-bit multiplier):   T:TL split view (T = upper 16 bits)
 *   - XAR0–XAR7 (32-bit auxiliary registers): AR0–AR7 are lower 16 bits
 *   - SP   (16-bit stack pointer)
 *   - PC   (22-bit program counter) — stored as 32-bit byte address
 *   - RPC  (22-bit return program counter)
 *   - ST0, ST1 (16-bit status registers)
 *   - IFR, IER (16-bit interrupt flag/enable)
 *   - DP   (16-bit data page pointer)
 *
 * Instructions are 16-bit (one word) or 32-bit (two words).
 * The XAR registers support indirect addressing with auto-increment/decrement.
 *
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */

#if (TARGET_LONG_BITS == 32)
#ifdef TARGET_PHYS_ADDR_BITS
#undef TARGET_PHYS_ADDR_BITS
#endif
#define TARGET_PHYS_ADDR_BITS 24
#else
#error "Only 32-bit target word size is supported for C2000."
#endif

#define TARGET_PAGE_BITS            12
#define TARGET_PHYS_ADDR_SPACE_BITS 24

#include "cpu_registers.h"
#include "cpu-defs.h"

/* ------------------------------------------------------------------ */
/* Exception indices                                                   */
/* ------------------------------------------------------------------ */

#define EXCP_RESET    0    /* Hardware reset                        */
#define EXCP_NMI      1    /* Non-maskable interrupt (XNMI)         */
#define EXCP_INT1     2    /* INT1–INT14 are 2–15                   */
#define EXCP_INT14   15
#define EXCP_DLOGINT 16    /* Data log interrupt                    */
#define EXCP_RTOSINT 17    /* RTOS interrupt                        */
#define EXCP_EMUINT  18    /* Emulation interrupt                   */
#define EXCP_NMI_EXT 19    /* NMI (from PIE)                        */
#define EXCP_PIE_INT 32    /* PIE multiplexed interrupts base        */
#define EXCP_UNDEF   63    /* Undefined/illegal instruction          */

/* ------------------------------------------------------------------ */
/* ST0 status register bits                                            */
/* ------------------------------------------------------------------ */
#define ST0_SXM_SHIFT  0
#define ST0_SXM        (1u << ST0_SXM_SHIFT)    /* sign extension mode  */
#define ST0_OVM_SHIFT  1
#define ST0_OVM        (1u << ST0_OVM_SHIFT)    /* overflow mode        */
#define ST0_TC_SHIFT   2
#define ST0_TC         (1u << ST0_TC_SHIFT)     /* test/compare flag    */
#define ST0_C_SHIFT    3
#define ST0_C          (1u << ST0_C_SHIFT)      /* carry                */
#define ST0_Z_SHIFT    4
#define ST0_Z          (1u << ST0_Z_SHIFT)      /* zero                 */
#define ST0_N_SHIFT    5
#define ST0_N          (1u << ST0_N_SHIFT)      /* negative             */
#define ST0_V_SHIFT    6
#define ST0_V          (1u << ST0_V_SHIFT)      /* overflow             */
#define ST0_PM_SHIFT   7
#define ST0_PM_MASK    (0x7u << ST0_PM_SHIFT)   /* product mode shift   */
#define ST0_OVC_SHIFT  10
#define ST0_OVC_MASK   (0x3Fu << ST0_OVC_SHIFT) /* overflow counter     */

/* ------------------------------------------------------------------ */
/* ST1 status register bits                                            */
/* ------------------------------------------------------------------ */
#define ST1_INTM_SHIFT  0
#define ST1_INTM        (1u << ST1_INTM_SHIFT)  /* interrupt mask       */
#define ST1_DBGM_SHIFT  1
#define ST1_DBGM        (1u << ST1_DBGM_SHIFT)  /* debug mask           */
#define ST1_PAGE0_SHIFT 2
#define ST1_PAGE0       (1u << ST1_PAGE0_SHIFT) /* stack/direct mode    */
#define ST1_VMAP_SHIFT  3
#define ST1_VMAP        (1u << ST1_VMAP_SHIFT)  /* vector map           */
#define ST1_SPA_SHIFT   4
#define ST1_SPA         (1u << ST1_SPA_SHIFT)   /* stack pointer align  */
#define ST1_LOOP_SHIFT  5
#define ST1_LOOP        (1u << ST1_LOOP_SHIFT)  /* loop instruction active */
#define ST1_EALLOW_SHIFT 6
#define ST1_EALLOW      (1u << ST1_EALLOW_SHIFT) /* EMIF write enable   */
#define ST1_IDLESTAT_SHIFT 7
#define ST1_IDLESTAT    (1u << ST1_IDLESTAT_SHIFT) /* IDLE active       */
#define ST1_ARP_SHIFT    8
#define ST1_ARP_MASK     (0x7u << ST1_ARP_SHIFT)  /* auxiliary register pointer */
#define ST1_XF_SHIFT    12
#define ST1_XF          (1u << ST1_XF_SHIFT)    /* XF output signal     */
#define ST1_M0M1MAP_SHIFT 11
#define ST1_M0M1MAP     (1u << ST1_M0M1MAP_SHIFT)/* M0/M1 memory map   */

/* Reset asserts CPU_INTERRUPT_TGT_EXT_0 */
#define CPU_INTERRUPT_RESET CPU_INTERRUPT_TGT_EXT_0

#define NB_MMU_MODES 1

/* +---------------------------------------+
 * | ALL FIELDS WHICH STATE MUST BE STORED |
 * | DURING SERIALIZATION SHOULD BE PLACED |
 * | BEFORE >CPU_COMMON< SECTION.          |
 * +---------------------------------------+ */
typedef struct CPUState {
    /* Main registers */
    uint32_t acc;   /* 32-bit accumulator (AH:AL)                */
    uint32_t p;     /* 32-bit product register (PH:PL)           */
    uint32_t xt;    /* 32-bit multiplier register (T:TL)         */

    /* Auxiliary registers XAR0–XAR7 */
    uint32_t xar[8];

    /* Stack pointer (16-bit, word addressed) */
    uint32_t sp;

    /* Program counter (22-bit, word addressed; stored as byte address) */
    uint32_t pc;

    /* Return program counter */
    uint32_t rpc;

    /* Status registers */
    uint32_t st0;
    uint32_t st1;

    /* Interrupt registers */
    uint32_t ifr;
    uint32_t ier;

    /* Data page pointer */
    uint32_t dp;

    /* ******* CPU_COMMON starts here ******* */

    CPU_COMMON

    /* ******* Not serialised ******* */

} CPUState;

/* Declarations moved to end of file after cpu-all.h / exec-all.h */

/* MMU: flat mapping */
#define MMU_USER_IDX     0
#define MMU_MODE0_SUFFIX _user

static inline int cpu_mmu_index(CPUState *env)
{
    return 0;
}

int cpu_handle_mmu_fault(CPUState *env, target_ulong address,
                         int access_type, int mmu_idx,
                         int access_width, int no_page_fault,
                         target_phys_addr_t *paddr);

#include "cpu-all.h"

typedef struct DisasContext {
    struct DisasContextBase base;
} DisasContext;

#include "exec-all.h"

static inline int cpu_interrupts_enabled(CPUState *env)
{
    /* INTM bit in ST1: 0 = interrupts enabled, 1 = masked */
    return (env->st1 & ST1_INTM) == 0;
}

static inline bool cpu_has_work(CPUState *env)
{
    env->wfi = env->wfi && !(is_interrupt_pending(env, CPU_INTERRUPT_HARD) && cpu_interrupts_enabled(env));
    return !env->wfi;
}

static inline void cpu_get_tb_cpu_state(CPUState *env, target_ulong *pc,
                                         target_ulong *cs_base, int *flags)
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

#pragma once

/*
 * dsPIC33 CPU emulation
 *
 * dsPIC33 is a 16-bit modified Harvard architecture from Microchip.
 * Working registers W0-W15 (16-bit), with W15 serving as the stack pointer.
 * PC is 23-bit (program-word addressed), stored here as a 32-bit byte address.
 * Data memory is little-endian; instructions are 24-bit (stored as 32-bit words
 * in the 8-bit-phantom program flash).
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
#error "Only 32-bit target word size is supported for dsPIC33."
#endif

#define TARGET_PAGE_BITS            12  /* 4 KB pages */
#define TARGET_PHYS_ADDR_SPACE_BITS 24

#include "cpu_registers.h"
#include "cpu-defs.h"

/* Exception / trap indices */
#define EXCP_RESET          0   /* Power-on Reset                 */
#define EXCP_OSC_FAIL       2   /* Oscillator Fail trap           */
#define EXCP_ADDR_ERROR     3   /* Address Error trap             */
#define EXCP_STACK_ERROR    4   /* Stack Error trap               */
#define EXCP_MATH_ERROR     5   /* Math Error trap                */
#define EXCP_DMA_ERROR      6   /* DMA Error trap                 */
#define EXCP_SOFT_TRAP      7   /* Software trap (TRAP)           */
#define EXCP_INT_FIRST      8   /* First IRQ vector (INT0)        */
#define EXCP_UNDEF         63   /* Undefined/illegal instruction  */

/* STATUS register (SReg) bit definitions */
#define SR_C_SHIFT      0
#define SR_C            (1u << SR_C_SHIFT)   /* Carry                */
#define SR_Z_SHIFT      1
#define SR_Z            (1u << SR_Z_SHIFT)   /* Zero                 */
#define SR_OV_SHIFT     2
#define SR_OV           (1u << SR_OV_SHIFT)  /* Overflow             */
#define SR_N_SHIFT      3
#define SR_N            (1u << SR_N_SHIFT)   /* Negative             */
#define SR_RA_SHIFT     4
#define SR_RA           (1u << SR_RA_SHIFT)  /* REPEAT active        */
#define SR_IPL_SHIFT    5
#define SR_IPL_MASK     (0x7u << SR_IPL_SHIFT) /* CPU Interrupt Priority Level */
#define SR_DC_SHIFT     8
#define SR_DC           (1u << SR_DC_SHIFT)  /* Digit carry          */
#define SR_DA_SHIFT     9
#define SR_DA           (1u << SR_DA_SHIFT)  /* DO loop active       */
#define SR_SAB_SHIFT    10
#define SR_SAB          (1u << SR_SAB_SHIFT) /* acc B saturated      */
#define SR_SAA_SHIFT    11
#define SR_SAA          (1u << SR_SAA_SHIFT) /* acc A saturated      */
#define SR_OAB_SHIFT    12
#define SR_OAB          (1u << SR_OAB_SHIFT) /* acc B overflow       */
#define SR_OA_SHIFT     13
#define SR_OA           (1u << SR_OA_SHIFT)  /* acc A overflow       */

/* CORCON register bits */
#define CORCON_IF_SHIFT  0
#define CORCON_IF        (1u << CORCON_IF_SHIFT)  /* Integer/Fractional mode */
#define CORCON_RND_SHIFT 1
#define CORCON_RND       (1u << CORCON_RND_SHIFT) /* Rounding mode           */
#define CORCON_PSV_SHIFT 2
#define CORCON_PSV       (1u << CORCON_PSV_SHIFT) /* PSV enable              */
#define CORCON_IPL3_SHIFT 3
#define CORCON_IPL3      (1u << CORCON_IPL3_SHIFT) /* IPL3 (shadow CPU IPL)  */

/* Interrupt: CPU_INTERRUPT_RESET piggy-backs on tlib's target ext 0 */
#define CPU_INTERRUPT_RESET CPU_INTERRUPT_TGT_EXT_0

#define NB_MMU_MODES 1

/* +---------------------------------------+
 * | ALL FIELDS WHICH STATE MUST BE STORED |
 * | DURING SERIALIZATION SHOULD BE PLACED |
 * | BEFORE >CPU_COMMON< SECTION.          |
 * +---------------------------------------+ */
typedef struct CPUState {
    /* Working registers W0-W15 (16-bit values stored in 32-bit slots) */
    uint32_t w[16];

    /* Program Counter (byte address; PC<0> always 0 — instruction boundaries
     * are at even byte addresses since each instruction word is 4 bytes wide
     * in the phantom-byte scheme, but the effective PC increment is 2 per
     * instruction word in word-address terms). */
    uint32_t pc;

    /* Status register */
    uint32_t status;

    /* Special Function Registers */
    uint32_t tblpag;   /* Table Page register   (bits 7:0 used)  */
    uint32_t psvpag;   /* PSV Page register     (bits 7:0 used)  */
    uint32_t rcount;   /* REPEAT count register (bits 13:0)      */
    uint32_t dcount;   /* DO loop count         (bits 13:0)      */
    uint32_t corcon;   /* Core Control register                  */
    uint32_t disicnt;  /* DISI counter          (bits 13:0)      */

    /* ******* CPU_COMMON starts here ******* */

    CPU_COMMON

    /* ******* Fields after CPU_COMMON are not serialised ******* */

} CPUState;

/* Declarations moved to end of file after cpu-all.h / exec-all.h */

/* MMU: flat mapping — single mode */
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
    /* Interrupts are enabled when IPL < 7 (non-NMI) and DISI == 0 */
    uint32_t ipl = (env->status >> SR_IPL_SHIFT) & 0x7u;
    return (ipl < 7) && (env->disicnt == 0);
}

static inline bool cpu_has_work(CPUState *env)
{
    env->wfi = env->wfi && !(is_interrupt_pending(env, CPU_INTERRUPT_HARD) && cpu_interrupts_enabled(env));
    return !env->wfi;
}

static inline void cpu_get_tb_cpu_state(CPUState *env, target_ulong *pc,
                                         target_ulong *cs_base, int *flags)
{
    *pc       = env->pc;
    *cs_base  = 0;
    *flags    = 0;
}

static inline void cpu_pc_from_tb(CPUState *env, TranslationBlock *tb)
{
    env->pc = tb->pc;
}

void do_interrupt(CPUState *env);

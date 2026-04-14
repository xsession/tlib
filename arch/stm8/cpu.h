/*
 * STM8 CPU definitions for tlib.
 *
 * The STM8 is an 8-bit modified-Harvard architecture by STMicroelectronics:
 *   - A:   8-bit accumulator
 *   - X/Y: 16-bit index registers
 *   - SP:  16-bit stack pointer
 *   - PC:  24-bit program counter (byte addressed; stored in 32-bit slot)
 *   - CC:  8-bit condition code register
 *
 * Program and data memory share a 16 MB address space.
 *
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */

#pragma once

#define NB_MMU_MODES    1
#define TARGET_PAGE_BITS 8
#define TARGET_PHYS_ADDR_SPACE_BITS 24

#include "cpu_registers.h"
#include "cpu-defs.h"

/* ------------------------------------------------------------------ */
/* CC (Condition Code) bit definitions                                 */
/* ------------------------------------------------------------------ */
#define CC_C    (1u << 0)  /* carry                     */
#define CC_Z    (1u << 1)  /* zero                      */
#define CC_N    (1u << 2)  /* negative                  */
#define CC_I0   (1u << 3)  /* interrupt mask bit 0      */
#define CC_H    (1u << 4)  /* half carry                */
#define CC_I1   (1u << 5)  /* interrupt mask bit 1      */
#define CC_V    (1u << 7)  /* overflow                  */

/* Interrupts masked when I1:I0 == 1:1 (priority level 3 — highest masked) */
#define CC_I_MASK   (CC_I0 | CC_I1)

/* ------------------------------------------------------------------ */
/* Exception vectors                                                   */
/* ------------------------------------------------------------------ */
#define EXCP_RESET      0  /* hardware reset                          */
#define EXCP_TRAP       1  /* TRAP software interrupt (0x8000)        */
#define EXCP_NMI        2  /* NMI/TLI at 0x8008                      */
#define EXCP_INT_FIRST  3  /* first maskable interrupt (IRQ0 = 0x8010)*/
#define EXCP_UNDEF     63  /* undefined instruction                   */

#define CPU_INTERRUPT_RESET CPU_INTERRUPT_TGT_EXT_0

/* ------------------------------------------------------------------ */
/* CPU state                                                           */
/* ------------------------------------------------------------------ */
typedef struct CPUState {
    CPU_COMMON

    uint32_t a;    /* accumulator (8-bit)         */
    uint32_t x;    /* index register X (16-bit)   */
    uint32_t y;    /* index register Y (16-bit)   */
    uint32_t sp;   /* stack pointer (16-bit)      */
    uint32_t pc;   /* program counter (24-bit)    */
    uint32_t cc;   /* condition code register (8-bit) */
} CPUState;

/* ------------------------------------------------------------------ */
/* Inline helpers                                                      */
/* ------------------------------------------------------------------ */

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
    /* Interrupts are enabled when I1:I0 != 1:1 */
    return (env->cc & CC_I_MASK) != CC_I_MASK;
}

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

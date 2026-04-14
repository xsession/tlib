/*
 * Intel 8051 (MCS-51) CPU definitions for tlib.
 *
 * Classic 8-bit Harvard architecture:
 *   - Accumulator (A), B register, DPTR (16-bit), SP (8-bit)
 *   - 128 bytes internal RAM (+ 128 upper for 8052), 256 byte SFR space
 *   - 4 register banks × 8 registers (R0-R7 aliased to RAM 0x00-0x1F)
 *   - Bit-addressable region: 0x20-0x2F (RAM), 0x80+ (SFR)
 *   - 16-bit PC, external data bus 8-bit, code bus 8-bit
 *
 * Memory map in Renode:
 *   CODE    : 0x000000               (up to 64 KB)
 *   IRAM    : MCS51_IRAM_BASE        (internal data)
 *   XDATA   : MCS51_XDATA_BASE       (external data up to 64 KB)
 *   SFR     : MCS51_SFR_BASE         (0x80-0xFF, byte-addressed)
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
/* Memory-space bases (non-overlapping in Renode flat map)             */
/* ------------------------------------------------------------------ */
#define MCS51_IRAM_BASE   0x100000u   /* internal RAM 0x00-0xFF  */
#define MCS51_XDATA_BASE  0x200000u   /* external data 0x0000-0xFFFF */
#define MCS51_SFR_BASE    0x300000u   /* SFR 0x80-0xFF           */

/* ------------------------------------------------------------------ */
/* PSW (Program Status Word) bits — SFR 0xD0                          */
/* ------------------------------------------------------------------ */
#define PSW_P    (1u << 0)  /* parity                   */
#define PSW_UD   (1u << 1)  /* user-defined             */
#define PSW_OV   (1u << 2)  /* overflow                 */
#define PSW_RS0  (1u << 3)  /* register bank select 0   */
#define PSW_RS1  (1u << 4)  /* register bank select 1   */
#define PSW_F0   (1u << 5)  /* general-purpose flag     */
#define PSW_AC   (1u << 6)  /* auxiliary carry           */
#define PSW_CY   (1u << 7)  /* carry                    */

/* IE (Interrupt Enable) bits — SFR 0xA8 */
#define IE_EA    (1u << 7)  /* global interrupt enable   */
#define IE_ES    (1u << 4)  /* serial interrupt          */
#define IE_ET1   (1u << 3)  /* timer 1                   */
#define IE_EX1   (1u << 2)  /* external 1                */
#define IE_ET0   (1u << 1)  /* timer 0                   */
#define IE_EX0   (1u << 0)  /* external 0                */

/* ------------------------------------------------------------------ */
/* Exception vectors                                                   */
/* ------------------------------------------------------------------ */
#define EXCP_RESET      0    /* power-on / RST pin             */
#define EXCP_EXT0       1    /* external interrupt 0 → 0x0003  */
#define EXCP_TIMER0     2    /* timer 0 overflow   → 0x000B   */
#define EXCP_EXT1       3    /* external interrupt 1 → 0x0013  */
#define EXCP_TIMER1     4    /* timer 1 overflow   → 0x001B   */
#define EXCP_SERIAL     5    /* serial port        → 0x0023   */
#define EXCP_INT_FIRST  1
#define EXCP_UNDEF      63

#define CPU_INTERRUPT_RESET CPU_INTERRUPT_TGT_EXT_0

/* Vector address = 0x0003 + (excp - 1) * 8 */
#define MCS51_VECTOR_ADDR(n) (0x0003u + ((n) - 1u) * 8u)

/* ------------------------------------------------------------------ */
/* CPU state                                                           */
/* ------------------------------------------------------------------ */
typedef struct CPUState {
    CPU_COMMON

    uint32_t acc;     /* accumulator (A)                       */
    uint32_t b;       /* B register                            */
    uint32_t psw;     /* program status word                   */
    uint32_t sp;      /* stack pointer (8-bit, points into IRAM) */
    uint32_t dpl;     /* data pointer low                      */
    uint32_t dph;     /* data pointer high                     */
    uint32_t pc;      /* 16-bit program counter                */
    uint32_t ie;      /* interrupt enable register             */
    uint32_t ip;      /* interrupt priority register           */
} CPUState;

static inline int cpu_mmu_index(CPUState *env)
{
    return 0;
}

static inline int cpu_interrupts_enabled(CPUState *env)
{
    return (env->ie & IE_EA) != 0;
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

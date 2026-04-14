/*
 * Atmel AVR 8-bit CPU definitions for tlib.
 *
 * The AVR is an 8-bit RISC Harvard architecture:
 *   - R0-R31: 32 general-purpose 8-bit registers (stored in 32-bit slots)
 *   - X (R27:R26), Y (R29:R28), Z (R31:R30): 16-bit pointer register pairs
 *   - SREG: status register (8-bit)
 *   - SP:   stack pointer (16-bit, in I/O space but modelled here)
 *   - PC:   program counter; byte-addressed; up to 22 bits
 *
 * Program memory lives at 0x000000 in the system bus.
 * Data memory (SRAM + I/O) is offset by AVR_DATA_BASE (0x800000)
 * so that data-space address 0x0100 maps to bus address 0x800100.
 *
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */

#pragma once

/* AVR has no MMU; single flat mode.  Required before cpu-defs.h. */
#define NB_MMU_MODES    1
#define TARGET_PAGE_BITS 8    /* 256-byte pages — matches AVR I/O alignment */
#define TARGET_PHYS_ADDR_SPACE_BITS 24  /* up to 16 MB address space */

#include "cpu-defs.h"

/* ------------------------------------------------------------------ */
/* Data-space base                                                     */
/* ------------------------------------------------------------------ */
#define AVR_DATA_BASE  0x800000u

/* ------------------------------------------------------------------ */
/* SREG bit definitions                                                */
/* ------------------------------------------------------------------ */
#define SREG_C   (1u << 0)  /* carry                    */
#define SREG_Z   (1u << 1)  /* zero                     */
#define SREG_N   (1u << 2)  /* negative                 */
#define SREG_V   (1u << 3)  /* two's complement overflow */
#define SREG_S   (1u << 4)  /* sign = N XOR V           */
#define SREG_H   (1u << 5)  /* half carry               */
#define SREG_T   (1u << 6)  /* bit copy storage         */
#define SREG_I   (1u << 7)  /* global interrupt enable  */

/* ------------------------------------------------------------------ */
/* Exception vectors (table index = interrupt number in reset vector  */
/* table; each pair of words = one vector)                            */
/* ------------------------------------------------------------------ */
#define EXCP_RESET     0  /* hardware reset              */
#define EXCP_INT0      1  /* external interrupt 0        */
#define EXCP_INT1      2  /* external interrupt 1        */
#define EXCP_INT_FIRST 1  /* first maskable interrupt    */
#define EXCP_UNDEF    63  /* undefined instruction       */

/* ------------------------------------------------------------------ */
/* CPU state                                                           */
/* ------------------------------------------------------------------ */
typedef struct CPUState {
    CPU_COMMON

    /* General-purpose registers R0-R31 (8-bit values in 32-bit slots) */
    uint32_t r[32];

    /* Core registers */
    uint32_t pc;     /* byte-addressed program counter                */
    uint32_t sp;     /* 16-bit stack pointer (data address)           */
    uint32_t sreg;   /* status register (8-bit)                       */

    /* Extended address registers (ATmega2560 etc.) */
    uint32_t rampz;  /* RAMPZ — extended Z-pointer high byte          */
    uint32_t rampx;  /* RAMPX                                         */
    uint32_t rampy;  /* RAMPY                                         */
    uint32_t eind;   /* EIND — extended indirect jump byte            */
} CPUState;

static inline int cpu_mmu_index(CPUState *env)
{
    return 0;  /* single flat address space */
}

int cpu_handle_mmu_fault(CPUState *env, target_ulong address,
                         int access_type, int mmu_idx,
                         int access_width, int no_page_fault,
                         target_phys_addr_t *paddr);

#include "cpu-all.h"

/* DisasContext must be a complete type for translate-all.c */
typedef struct DisasContext {
    struct DisasContextBase base;
} DisasContext;

#include "exec-all.h"

/* ------------------------------------------------------------------ */
/* Inline helpers required by tlib                                     */
/* ------------------------------------------------------------------ */

static inline int cpu_interrupts_enabled(CPUState *env)
{
    return (env->sreg & SREG_I) != 0;
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

#pragma once

/*
 * dsPIC33 register index definitions.
 *
 * These indices must match the switch cases in cpu_registers.c and
 * the register names used in the .tt template for C# code generation.
 *
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */

#include "cpu-defs.h"

typedef enum {
    /* Working registers W0-W15 */
    W0  =  0,
    W1  =  1,
    W2  =  2,
    W3  =  3,
    W4  =  4,
    W5  =  5,
    W6  =  6,
    W7  =  7,
    W8  =  8,
    W9  =  9,
    W10 = 10,
    W11 = 11,
    W12 = 12,
    W13 = 13,
    W14 = 14,
    W15 = 15,   /* also Frame Pointer / Stack Pointer */

    /* Core control/status */
    PC      = 16,
    STATUS  = 17,

    /* Special Function Registers */
    TBLPAG  = 18,
    PSVPAG  = 19,
    RCOUNT  = 20,
    DCOUNT  = 21,
    CORCON  = 22,
    DISICNT = 23,
} Registers;

/* Stack pointer is W15 */
#define SP_REG W15
/* Frame pointer convention uses W14 */
#define FP_REG W14

/* Return address register — dsPIC33 uses the hardware stack, not a GPR,
 * but W7 is commonly the first scratch by ABI.  Set to W15 as a safe
 * fallback so the profiler can unwind. */
#define RA                    W15
#define RETURN_ADDRESS_OFFSET 0

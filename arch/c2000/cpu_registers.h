#pragma once

/*
 * TMS320C28x (C2000) register index definitions.
 *
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */

#include "cpu-defs.h"

typedef enum {
    /* Main registers */
    ACC  =  0,   /* 32-bit accumulator             */
    AH   =  1,   /* upper 16 bits of ACC           */
    AL   =  2,   /* lower 16 bits of ACC           */
    P    =  3,   /* 32-bit product register        */
    PH   =  4,   /* upper 16 bits of P             */
    PL   =  5,   /* lower 16 bits of P             */
    XT   =  6,   /* 32-bit multiplier register     */
    T    =  7,   /* upper 16 bits of XT (T reg)    */

    /* 32-bit auxiliary registers */
    XAR0 =  8,
    XAR1 =  9,
    XAR2 = 10,
    XAR3 = 11,
    XAR4 = 12,
    XAR5 = 13,
    XAR6 = 14,
    XAR7 = 15,

    /* 16-bit lower halves of auxiliary registers */
    AR0  = 16,
    AR1  = 17,
    AR2  = 18,
    AR3  = 19,
    AR4  = 20,
    AR5  = 21,
    AR6  = 22,
    AR7  = 23,

    /* Control registers */
    SP   = 24,   /* Stack Pointer (16-bit)         */
    PC   = 25,   /* Program Counter (22-bit)       */
    RPC  = 26,   /* Return Program Counter         */
    ST0  = 27,   /* Status Register 0              */
    ST1  = 28,   /* Status Register 1              */
    IFR  = 29,   /* Interrupt Flag Register        */
    IER  = 30,   /* Interrupt Enable Register      */
    DP   = 31,   /* Data Page Pointer              */
} Registers;

/* Stack pointer is SP */
#define SP_REG SP
/* No separate frame pointer — XAR1 used by ABI convention */
#define FP_REG XAR1

/* Return address stored in RPC; offset 0 (stored directly, not pushed) */
#define RA                    RPC
#define RETURN_ADDRESS_OFFSET 0

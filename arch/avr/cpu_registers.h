/*
 * AVR register index definitions.
 *
 * These indices are used by get_reg_pointer_32() and by the C# side
 * via tlib_get/set_register_value_32.
 *
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */

#pragma once

typedef enum {
    AVR_REG_R0  =  0,
    AVR_REG_R1  =  1,
    AVR_REG_R2  =  2,
    AVR_REG_R3  =  3,
    AVR_REG_R4  =  4,
    AVR_REG_R5  =  5,
    AVR_REG_R6  =  6,
    AVR_REG_R7  =  7,
    AVR_REG_R8  =  8,
    AVR_REG_R9  =  9,
    AVR_REG_R10 = 10,
    AVR_REG_R11 = 11,
    AVR_REG_R12 = 12,
    AVR_REG_R13 = 13,
    AVR_REG_R14 = 14,
    AVR_REG_R15 = 15,
    AVR_REG_R16 = 16,
    AVR_REG_R17 = 17,
    AVR_REG_R18 = 18,
    AVR_REG_R19 = 19,
    AVR_REG_R20 = 20,
    AVR_REG_R21 = 21,
    AVR_REG_R22 = 22,
    AVR_REG_R23 = 23,
    AVR_REG_R24 = 24,
    AVR_REG_R25 = 25,
    AVR_REG_R26 = 26,   /* XL */
    AVR_REG_R27 = 27,   /* XH */
    AVR_REG_R28 = 28,   /* YL */
    AVR_REG_R29 = 29,   /* YH */
    AVR_REG_R30 = 30,   /* ZL */
    AVR_REG_R31 = 31,   /* ZH */
    AVR_REG_PC  = 32,
    AVR_REG_SREG = 33,
    AVR_REG_SP   = 34,
    AVR_REG_RAMPZ = 35,
    AVR_REG_EIND  = 36,
    AVR_REG_COUNT = 37,
} AVRRegisters;

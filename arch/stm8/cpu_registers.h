/*
 * STM8 register index definitions.
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */

#pragma once

typedef enum {
    STM8_REG_A  = 0,
    STM8_REG_X  = 1,
    STM8_REG_XH = 2,   /* high byte of X (scratch) */
    STM8_REG_XL = 3,   /* low  byte of X (scratch) */
    STM8_REG_Y  = 4,
    STM8_REG_YH = 5,   /* high byte of Y (scratch) */
    STM8_REG_YL = 6,   /* low  byte of Y (scratch) */
    STM8_REG_SP = 7,
    STM8_REG_PC = 8,
    STM8_REG_CC = 9,
    STM8_REG_COUNT = 10,
} STM8Registers;

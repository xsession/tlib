/*
 * STM8 register accessor.
 * XH/XL and YH/YL are split views of X/Y kept in scratch buffers.
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */
#include "cpu.h"
#include "cpu_registers.h"

static uint32_t scratch_xh, scratch_xl, scratch_yh, scratch_yl;

static void sync_split_regs(void)
{
    scratch_xh = (cpu->x >> 8) & 0xFF;
    scratch_xl =  cpu->x       & 0xFF;
    scratch_yh = (cpu->y >> 8) & 0xFF;
    scratch_yl =  cpu->y       & 0xFF;
}

uint32_t *get_reg_pointer_32(int reg)
{
    sync_split_regs();
    switch((STM8Registers)reg)
    {
    case STM8_REG_A:  return &cpu->a;
    case STM8_REG_X:  return &cpu->x;
    case STM8_REG_XH: return &scratch_xh;
    case STM8_REG_XL: return &scratch_xl;
    case STM8_REG_Y:  return &cpu->y;
    case STM8_REG_YH: return &scratch_yh;
    case STM8_REG_YL: return &scratch_yl;
    case STM8_REG_SP: return &cpu->sp;
    case STM8_REG_PC: return &cpu->pc;
    case STM8_REG_CC: return &cpu->cc;
    default:          return NULL;
    }
}

CPU_REGISTER_ACCESSOR(32)

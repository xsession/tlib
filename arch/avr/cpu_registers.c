/*
 * AVR register accessor implementation.
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */

#include "cpu.h"
#include "cpu_registers.h"

uint32_t *get_reg_pointer_32(int reg)
{
    switch((AVRRegisters)reg)
    {
    case AVR_REG_R0:  case AVR_REG_R1:  case AVR_REG_R2:  case AVR_REG_R3:
    case AVR_REG_R4:  case AVR_REG_R5:  case AVR_REG_R6:  case AVR_REG_R7:
    case AVR_REG_R8:  case AVR_REG_R9:  case AVR_REG_R10: case AVR_REG_R11:
    case AVR_REG_R12: case AVR_REG_R13: case AVR_REG_R14: case AVR_REG_R15:
    case AVR_REG_R16: case AVR_REG_R17: case AVR_REG_R18: case AVR_REG_R19:
    case AVR_REG_R20: case AVR_REG_R21: case AVR_REG_R22: case AVR_REG_R23:
    case AVR_REG_R24: case AVR_REG_R25: case AVR_REG_R26: case AVR_REG_R27:
    case AVR_REG_R28: case AVR_REG_R29: case AVR_REG_R30: case AVR_REG_R31:
        return &cpu->r[reg];
    case AVR_REG_PC:    return &cpu->pc;
    case AVR_REG_SREG:  return &cpu->sreg;
    case AVR_REG_SP:    return &cpu->sp;
    case AVR_REG_RAMPZ: return &cpu->rampz;
    case AVR_REG_EIND:  return &cpu->eind;
    default:
        return NULL;
    }
}

CPU_REGISTER_ACCESSOR(32)

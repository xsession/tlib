/*
 * dsPIC33 register access implementation.
 *
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */
#include <stdint.h>

#include "cpu.h"
#include "cpu_registers.h"

uint32_t *get_reg_pointer_32(int reg)
{
    switch(reg) {
        case W0 ... W15:
            return &(cpu->w[reg]);
        case PC:
            return &(cpu->pc);
        case STATUS:
            return &(cpu->status);
        case TBLPAG:
            return &(cpu->tblpag);
        case PSVPAG:
            return &(cpu->psvpag);
        case RCOUNT:
            return &(cpu->rcount);
        case DCOUNT:
            return &(cpu->dcount);
        case CORCON:
            return &(cpu->corcon);
        case DISICNT:
            return &(cpu->disicnt);
        default:
            return NULL;
    }
}

CPU_REGISTER_ACCESSOR(32)

#include "cpu.h"
#include "cpu_registers.h"

uint32_t *get_reg_pointer_32(int reg)
{
    switch(reg)
    {
    case WREG:   return &cpu->wreg;
    case BSR:    return &cpu->bsr;
    case STATUS: return &cpu->status;
    case PC:     return &cpu->pc;
    case FSR0:   return &cpu->fsr0;
    case FSR1:   return &cpu->fsr1;
    case FSR2:   return &cpu->fsr2;
    case STKPTR: return &cpu->stkptr;
    case TOS:    return &cpu->tos;
    case PRODH:  return &cpu->prodh;
    case PRODL:  return &cpu->prodl;
    case TABLAT: return &cpu->tablat;
    case TBLPTR: return &cpu->tblptr;
    case INTCON: return &cpu->intcon;
    default:     return NULL;
    }
}

CPU_REGISTER_ACCESSOR(32)

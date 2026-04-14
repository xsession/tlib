#include "cpu.h"
#include "cpu_registers.h"

uint32_t *get_reg_pointer_32(int reg)
{
    switch(reg)
    {
    case ACC:    return &cpu->acc;
    case B_REG:  return &cpu->b;
    case PSW:    return &cpu->psw;
    case SP_REG: return &cpu->sp;
    case DPL:    return &cpu->dpl;
    case DPH:    return &cpu->dph;
    case PC:     return &cpu->pc;
    case IE_REG: return &cpu->ie;
    case IP_REG: return &cpu->ip;
    default:     return NULL;
    }
}

CPU_REGISTER_ACCESSOR(32)

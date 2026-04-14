#include "cpu.h"
#include "cpu_registers.h"

/* All registers are treated as 32-bit slots internally.
 * Widths reported to GDB / Renode are defined in the C# layer. */

uint32_t *get_reg_pointer_32(int reg)
{
    switch(reg)
    {
    case W:          return &cpu->w;
    case PC:         return &cpu->pc;
    case STATUS_REG: return &cpu->status;
    case FSR:        return &cpu->fsr;
    case PCLATH:     return &cpu->pclath;
    case INTCON_REG: return &cpu->intcon;
    case OPTION_REG: return &cpu->option_reg;
    default:         return NULL;
    }
}

CPU_REGISTER_ACCESSOR(32)

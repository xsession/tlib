#pragma once

typedef enum {
    ACC     = 0,
    B_REG   = 1,
    PSW     = 2,
    SP_REG  = 3,
    DPL     = 4,
    DPH     = 5,
    PC      = 6,
    IE_REG  = 7,
    IP_REG  = 8,
    MCS51_REGISTERS_COUNT = 9
} MCS51Registers;

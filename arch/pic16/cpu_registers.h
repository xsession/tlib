#ifndef PIC16_CPU_REGISTERS_H
#define PIC16_CPU_REGISTERS_H

typedef enum {
    W          = 0,
    PC         = 1,
    STATUS_REG = 2,
    FSR        = 3,
    PCLATH     = 4,
    INTCON_REG = 5,
    OPTION_REG = 6,
    PIC16_REGISTERS_COUNT = 7
} PIC16Registers;

#endif /* PIC16_CPU_REGISTERS_H */

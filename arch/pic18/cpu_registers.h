#ifndef PIC18_CPU_REGISTERS_H
#define PIC18_CPU_REGISTERS_H

typedef enum {
    WREG    = 0,
    BSR     = 1,
    STATUS  = 2,
    PC      = 3,
    FSR0    = 4,
    FSR1    = 5,
    FSR2    = 6,
    STKPTR  = 7,
    TOS     = 8,
    PRODH   = 9,
    PRODL   = 10,
    TABLAT  = 11,
    TBLPTR  = 12,
    INTCON  = 13,
    PIC18_REGISTERS_COUNT = 14
} PIC18Registers;

#endif /* PIC18_CPU_REGISTERS_H */

/*
 * TMS320C28x (C2000) register access implementation.
 *
 * AH/AL are the upper/lower halves of ACC; PH/PL of P; T of XT.
 * AR0-AR7 are the lower 16 bits of XAR0-XAR7.
 * These "split" views provide a 32-bit pointer into a separate scratch buffer
 * that is synced on read.  For simplicity we compute them lazily.
 *
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */
#include <stdint.h>
#include "cpu.h"
#include "cpu_registers.h"

/* Scratch storage for split-view registers that map to half-words of 32-bit regs */
static uint32_t scratch_ah, scratch_al;
static uint32_t scratch_ph, scratch_pl;
static uint32_t scratch_t;
static uint32_t scratch_ar[8];

static void sync_split_regs(void)
{
    scratch_ah = (cpu->acc >> 16) & 0xFFFF;
    scratch_al =  cpu->acc        & 0xFFFF;
    scratch_ph = (cpu->p   >> 16) & 0xFFFF;
    scratch_pl =  cpu->p          & 0xFFFF;
    scratch_t  = (cpu->xt  >> 16) & 0xFFFF;
    for(int i = 0; i < 8; i++) {
        scratch_ar[i] = cpu->xar[i] & 0xFFFF;
    }
}

uint32_t *get_reg_pointer_32(int reg)
{
    switch(reg) {
        case ACC:  return &(cpu->acc);
        case AH:   sync_split_regs(); return &scratch_ah;
        case AL:   sync_split_regs(); return &scratch_al;
        case P:    return &(cpu->p);
        case PH:   sync_split_regs(); return &scratch_ph;
        case PL:   sync_split_regs(); return &scratch_pl;
        case XT:   return &(cpu->xt);
        case T:    sync_split_regs(); return &scratch_t;
        case XAR0 ... XAR7: return &(cpu->xar[reg - XAR0]);
        case AR0  ... AR7:  sync_split_regs(); return &scratch_ar[reg - AR0];
        case SP:   return &(cpu->sp);
        case PC:   return &(cpu->pc);
        case RPC:  return &(cpu->rpc);
        case ST0:  return &(cpu->st0);
        case ST1:  return &(cpu->st1);
        case IFR:  return &(cpu->ifr);
        case IER:  return &(cpu->ier);
        case DP:   return &(cpu->dp);
        default:   return NULL;
    }
}

CPU_REGISTER_ACCESSOR(32)

#pragma once

/*
 * dsPIC33 architecture callbacks — called from translated code back into C#.
 *
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */

#include <stdint.h>

/* Returns the best pending interrupt vector number, or 0 if none. */
int32_t tlib_find_best_interrupt(void);

/* Acknowledge (clear) an interrupt after acceptance. */
void tlib_acknowledge_interrupt(int32_t number);

/* Called when the CPU executes a PWRSAV #0 (Sleep) instruction. */
void tlib_on_cpu_halted(void);

/* Called when the CPU executes a PWRSAV #1 (Idle) instruction. */
void tlib_on_cpu_power_down(void);

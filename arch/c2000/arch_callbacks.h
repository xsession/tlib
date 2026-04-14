#pragma once

/*
 * TMS320C28x (C2000) architecture callbacks.
 *
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */

#include <stdint.h>

/* Find the highest-priority pending interrupt; returns vector number or 0. */
int32_t tlib_find_best_interrupt(void);

/* Acknowledge (clear) an interrupt after CPU acceptance. */
void tlib_acknowledge_interrupt(int32_t number);

/* Called when the CPU executes an IDLE instruction. */
void tlib_on_cpu_halted(void);

/* Called on power-down. */
void tlib_on_cpu_power_down(void);

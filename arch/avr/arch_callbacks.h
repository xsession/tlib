/*
 * AVR arch_callbacks — Renode ↔ tlib interrupt/halt callback declarations.
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */
#pragma once

#include "callbacks.h"

int32_t tlib_find_best_interrupt(void);
void    tlib_acknowledge_interrupt(int32_t interrupt_number);
void    tlib_on_cpu_halted(void);
void    tlib_on_cpu_power_down(void);

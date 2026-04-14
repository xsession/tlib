/*
 * TMS320C28x (C2000) architecture callbacks — default (weak) implementations.
 *
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */
#include "callbacks.h"
#include "arch_callbacks.h"

DEFAULT_INT_HANDLER1(int32_t tlib_find_best_interrupt, void)
DEFAULT_VOID_HANDLER1(void tlib_acknowledge_interrupt, int32_t interrupt_number)
DEFAULT_VOID_HANDLER0(void tlib_on_cpu_halted)
DEFAULT_VOID_HANDLER0(void tlib_on_cpu_power_down)

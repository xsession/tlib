#pragma once

/*
 * dsPIC33 architecture exports — C functions callable from C# via [Import].
 *
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */

#include <stdint.h>

/* Set the PC to a specific byte address (e.g. after a Reset vector lookup). */
void tlib_set_entry_point(uint32_t entry_point);

/* Clear the WFI (Wait-For-Interrupt) sleep flag — resumes execution. */
void tlib_clear_wfi(void);

/* Set the WFI flag — halts execution until an interrupt arrives. */
void tlib_set_wfi(void);

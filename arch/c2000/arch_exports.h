#pragma once

/*
 * TMS320C28x (C2000) architecture exports.
 *
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */

#include <stdint.h>

/* Set the PC to a specific byte address. */
void tlib_set_entry_point(uint32_t entry_point);

/* Clear WFI — resume execution. */
void tlib_clear_wfi(void);

/* Set WFI — halt until an interrupt arrives. */
void tlib_set_wfi(void);

/*
 * AVR arch_exports — functions exported from tlib to Renode C# layer.
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */
#pragma once

#include <stdint.h>

void tlib_set_entry_point(uint32_t entry);
void tlib_clear_wfi(void);
void tlib_set_wfi(void);

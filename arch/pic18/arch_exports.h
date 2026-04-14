#pragma once
#include <stdint.h>

void tlib_set_entry_point(uint32_t address);
void tlib_clear_wfi(void);
void tlib_set_wfi(void);

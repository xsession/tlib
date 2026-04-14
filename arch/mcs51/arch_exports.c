#include <stdint.h>
#include "cpu.h"
#include "unwind.h"
#include "arch_exports.h"

void tlib_set_entry_point(uint32_t entry)
{
    cpu->pc = entry & 0xFFFFu; /* 16-bit code space */
}
EXC_VOID_1(tlib_set_entry_point, uint32_t, entry)

void tlib_clear_wfi(void)
{
    cpu->wfi = 0;
}
EXC_VOID_0(tlib_clear_wfi)

void tlib_set_wfi(void)
{
    cpu->wfi = 1;
}
EXC_VOID_0(tlib_set_wfi)

#include "def-helper.h"

DEF_HELPER_2(raise_exception, void, env, i32)
DEF_HELPER_1(sleep,           void, env)
DEF_HELPER_1(wdt_reset,       void, env)

#include "def-helper.h"

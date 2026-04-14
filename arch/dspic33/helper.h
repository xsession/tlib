/*
 * dsPIC33 TCG helper declarations.
 *
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */
#include "def-helper.h"

DEF_HELPER_2(raise_exception,     void, env, i32)
DEF_HELPER_1(power_down,          void, env)
DEF_HELPER_1(wait_for_interrupt,   void, env)
DEF_HELPER_3(check_align,         void, env, tl, i32)

#include "def-helper.h"

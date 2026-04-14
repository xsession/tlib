/*
 * TMS320C28x (C2000) TCG helper declarations.
 *
 * Copyright (c) Antmicro
 * This file is licensed under the MIT License.
 */
#include "def-helper.h"

DEF_HELPER_2(raise_exception, void, env, i32)
DEF_HELPER_1(idle,            void, env)
DEF_HELPER_3(check_align,     void, env, tl, i32)

#include "def-helper.h"

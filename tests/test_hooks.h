#ifndef TEST_HOOKS_H
#define TEST_HOOKS_H

#include <ttypt/xy.h>

XY_HOOK_DECL(int, get_counter, int, dummy);
XY_HOOK_DECL(int, increment_counter, int, amount);

#endif

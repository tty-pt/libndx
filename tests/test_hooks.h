#ifndef TEST_HOOKS_H
#define TEST_HOOKS_H

#include <ttypt/ndx.h>

NDX_HOOK_DECL(int, get_counter, int, dummy);
NDX_HOOK_DECL(int, increment_counter, int, amount);

#endif

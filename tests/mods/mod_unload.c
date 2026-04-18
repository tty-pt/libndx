/*
 * mod_unload: test module for ndx_unload / ndx_reload tests.
 *
 * Exports:
 *   get_counter(dummy)   — returns a counter that increments on each call
 *   ndx_install()        — required entry point
 */
#include <ttypt/ndx-mod.h>

NDX_DEF(int, get_counter, int, dummy);

static int call_count = 0;

MODULE_API int get_counter(int dummy) {
	(void)dummy;
	return ++call_count;
}

MODULE_API void ndx_install(void) {}

/*
 * mod_unload: test module for xy_unload / xy_reload tests.
 *
 * Exports:
 *   get_counter(dummy)   — returns a counter that increments on each call
 *   xy_install()        — required entry point
 */
#include <ttypt/xy-mod.h>

XY_LISTENER(int, get_counter, int, dummy);

static int call_count = 0;

XY_MODULE_API int get_counter(int dummy) {
	(void)dummy;
	return ++call_count;
}

XY_MODULE_API void xy_install(void) {}

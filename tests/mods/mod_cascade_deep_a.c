/*
 * mod_cascade_deep_a: top of a 3-level cascade chain.
 * Loads mod_cascade_deep_b during xy_install.
 * Returns sentinel 11.
 */
#include <ttypt/xy-mod.h>

XY_LISTENER(int, get_counter, int, dummy);

XY_MODULE_API int get_counter(int dummy) {
	(void)dummy;
	return 11;
}

XY_MODULE_API void xy_install(void) {
	xy_load("tests/mods/mod_cascade_deep_b");
}

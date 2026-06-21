/*
 * mod_cascade_deep_c: leaf of a 3-level cascade chain.
 * Returns sentinel 33.
 */
#include <ttypt/xy-mod.h>

XY_LISTENER(int, get_counter, int, dummy);

XY_MODULE_API int get_counter(int dummy) {
	(void)dummy;
	return 33;
}

XY_MODULE_API void xy_install(void) {}

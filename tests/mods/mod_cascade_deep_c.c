/*
 * mod_cascade_deep_c: leaf of a 3-level cascade chain.
 * Returns sentinel 33.
 */
#include <ttypt/ndx-mod.h>

NDX_LISTENER(int, get_counter, int, dummy);

MODULE_API int get_counter(int dummy) {
	(void)dummy;
	return 33;
}

MODULE_API void ndx_install(void) {}

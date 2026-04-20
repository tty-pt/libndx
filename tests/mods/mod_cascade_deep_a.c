/*
 * mod_cascade_deep_a: top of a 3-level cascade chain.
 * Loads mod_cascade_deep_b during ndx_install.
 * Returns sentinel 11.
 */
#include <ttypt/ndx-mod.h>

NDX_LISTENER(int, get_counter, int, dummy);

MODULE_API int get_counter(int dummy) {
	(void)dummy;
	return 11;
}

MODULE_API void ndx_install(void) {
	ndx_load("tests/mods/mod_cascade_deep_b");
}

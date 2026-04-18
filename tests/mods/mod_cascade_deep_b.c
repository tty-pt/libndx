/*
 * mod_cascade_deep_b: middle of a 3-level cascade chain.
 * Loads mod_cascade_deep_c during ndx_install.
 * Returns sentinel 22.
 */
#include <ttypt/ndx-mod.h>

NDX_DEF(int, get_counter, int, dummy);

MODULE_API int get_counter(int dummy) {
	(void)dummy;
	return 22;
}

MODULE_API void ndx_install(void) {
	ndx_load("tests/mods/mod_cascade_deep_c");
}

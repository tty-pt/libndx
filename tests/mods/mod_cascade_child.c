/*
 * mod_cascade_child: loaded by mod_cascade_parent during ndx_install.
 * Returns sentinel 55 from get_counter so its presence is detectable
 * as the last module in dispatch order.
 */
#include <ttypt/ndx-mod.h>

NDX_DEF(int, get_counter, int, dummy);

MODULE_API int get_counter(int dummy) {
	(void)dummy;
	return 55;
}

MODULE_API void ndx_install(void) {}

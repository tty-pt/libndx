/*
 * mod_cascade_child: loaded by mod_cascade_parent during xy_install.
 * Returns sentinel 55 from get_counter so its presence is detectable
 * as the last module in dispatch order.
 */
#include <ttypt/xy-mod.h>

XY_LISTENER(int, get_counter, int, dummy);

XY_MODULE_API int get_counter(int dummy) {
	(void)dummy;
	return 55;
}

XY_MODULE_API void xy_install(void) {}

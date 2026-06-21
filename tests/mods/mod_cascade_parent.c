/*
 * mod_cascade_parent: loads mod_cascade_child during xy_install.
 * Returns sentinel 77 from get_counter; because the child is loaded
 * after, it runs last in dispatch and its value (55) wins.
 */
#include <ttypt/xy-mod.h>

XY_LISTENER(int, get_counter, int, dummy);

XY_MODULE_API int get_counter(int dummy) {
	(void)dummy;
	return 77;
}

XY_MODULE_API void xy_install(void) {
	xy_load("tests/mods/mod_cascade_child");
}

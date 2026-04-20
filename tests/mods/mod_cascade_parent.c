/*
 * mod_cascade_parent: loads mod_cascade_child during ndx_install.
 * Returns sentinel 77 from get_counter; because the child is loaded
 * after, it runs last in dispatch and its value (55) wins.
 */
#include <ttypt/ndx-mod.h>

NDX_LISTENER(int, get_counter, int, dummy);

MODULE_API int get_counter(int dummy) {
	(void)dummy;
	return 77;
}

MODULE_API void ndx_install(void) {
	ndx_load("tests/mods/mod_cascade_child");
}

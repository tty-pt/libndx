/*
 * mod_unload2: second test module for dispatch-order tests.
 *
 * Implements get_counter returning a fixed sentinel value (99) so that
 * when loaded after mod_unload, it wins the last-write-wins dispatch and
 * the return value is always 99.  This lets tests verify position in the
 * dispatch order without caring about call_count state.
 */
#include <ttypt/ndx-mod.h>

NDX_DEF(int, get_counter, int, dummy);

MODULE_API int get_counter(int dummy) {
	(void)dummy;
	return 99;
}

MODULE_API void ndx_install(void) {}

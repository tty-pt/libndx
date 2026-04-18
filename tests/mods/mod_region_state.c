/*
 * mod_region_state: test module for per-region state.
 *
 * Each region that loads this module gets an independent counter.
 * Exports:
 *   rs_increment(dummy)  — increments this region's counter, returns new value
 *   rs_get(dummy)        — returns this region's current counter
 *   ndx_region_cleanup() — tracks cleanup calls via a global flag
 */
#include <ttypt/ndx-mod.h>
#include <stdlib.h>

NDX_DEF(int, rs_increment, int, dummy);
NDX_DEF(int, rs_get,       int, dummy);

NDX_REGION_STATE {
	int counter;
};
NDX_REGION_INIT;

/* Global cleanup flag — test reads this to verify cleanup was called */
MODULE_API int rs_cleanup_called = 0;

MODULE_API void ndx_region_cleanup(void *state) {
	(void)state;
	rs_cleanup_called++;
}

/* ndx_claim = 2: requests a 2-bit child region (4 slots available from parent).
 * Allows up to 4 independent loads from the same parent, each in its own region. */
MODULE_API uint8_t ndx_claim = 2;

MODULE_API void ndx_install(void) {}

MODULE_API int rs_increment(int dummy) {
	(void)dummy;
	return ++NDX_RS->counter;
}

MODULE_API int rs_get(int dummy) {
	(void)dummy;
	return NDX_RS->counter;
}

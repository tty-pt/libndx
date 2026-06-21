/*
 * mod_region_state: test module for per-region state.
 *
 * Each region that loads this module gets an independent counter.
 * Exports:
 *   rs_increment(dummy)  — increments this region's counter, returns new value
 *   rs_get(dummy)        — returns this region's current counter
 *   xy_region_cleanup() — tracks cleanup calls via a global flag
 */
#include <ttypt/xy-mod.h>
#include <stdlib.h>

XY_LISTENER(int, rs_increment, int, dummy);
XY_LISTENER(int, rs_get,       int, dummy);

XY_REGION_STATE {
	int counter;
};
XY_REGION_INIT

/* Global cleanup flag — test reads this to verify cleanup was called */
XY_MODULE_API int rs_cleanup_called = 0;

XY_MODULE_API void xy_region_cleanup(void *state) {
	(void)state;
	rs_cleanup_called++;
}

/* xy_claim = 2: requests a 2-bit child region (4 slots available from parent).
 * Allows up to 4 independent loads from the same parent, each in its own region. */
XY_MODULE_API uint8_t xy_claim = 2;

XY_MODULE_API void xy_install(void) {}

XY_MODULE_API int rs_increment(int dummy) {
	(void)dummy;
	return ++XY_RS->counter;
}

XY_MODULE_API int rs_get(int dummy) {
	(void)dummy;
	return XY_RS->counter;
}

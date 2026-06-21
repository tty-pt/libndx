/*
 * mod_claim_simple: minimal module that claims a 2-bit child region.
 * Used to test xy_unload / xy_reload behaviour with claim-promoted modules.
 */
#include <ttypt/xy-mod.h>

XY_LISTENER(int, get_counter, int, dummy);

/* Request a 2-bit slice of the parent region */
XY_MODULE_API uint8_t xy_claim = 2;

XY_MODULE_API int get_counter(int dummy) {
	(void)dummy;
	return 33;
}

XY_MODULE_API void xy_install(void) {}

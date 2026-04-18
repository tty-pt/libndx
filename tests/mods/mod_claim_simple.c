/*
 * mod_claim_simple: minimal module that claims a 2-bit child region.
 * Used to test ndx_unload / ndx_reload behaviour with claim-promoted modules.
 */
#include <ttypt/ndx-mod.h>

NDX_DEF(int, get_counter, int, dummy);

/* Request a 2-bit slice of the parent region */
MODULE_API uint8_t ndx_claim = 2;

MODULE_API int get_counter(int dummy) {
	(void)dummy;
	return 33;
}

MODULE_API void ndx_install(void) {}

/*
 * mod_ptr_args_caller — module A: implements ptr_resolve which calls the
 * external hook ptr_lookup (provided by module B = mod_ptr_args).
 *
 * This mirrors the real-world topology in site/mods: poem.so calls
 * call_get_request_user which dispatches into auth.so; auth.so's body
 * then calls call_get_session_user which dispatches back into auth.so.
 *
 * The key property: module A has its own static XY_DECL adapter for
 * ptr_lookup (no .call, hook_id==-1), and lookup resolution happens on
 * first invocation inside libxylem's hot path.
 */
#include "ptr_args.h"
#include "../../src/papi.h"
#include <string.h>

xy_t xy;

/* ptr_resolve is this module's own XY_LISTENER — but invokes ptr_lookup
 * from module B via its own XY_DECL adapter (ptr_lookup_adapter here
 * is a distinct static instance from the one in mod_ptr_args.so). */
XY_LISTENER(const char *, ptr_resolve_remote, int, which)
{
	const char *key;
	switch (which) {
		case 1: key = "tok-alice"; break;
		case 2: key = "tok-bob";   break;
		case 3: key = "tok-carol"; break;
		default: return NULL;
	}
	return ptr_lookup(key);
}

XY_MODULE_API void xy_install(void) {}

XY_MODULE_API xy_t* get_xy_ptr(void) {
	return &xy;
}

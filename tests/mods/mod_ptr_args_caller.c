/*
 * mod_ptr_args_caller — module A: implements ptr_resolve which calls the
 * external hook ptr_lookup (provided by module B = mod_ptr_args).
 *
 * This mirrors the real-world topology in site/mods: poem.so calls
 * call_get_request_user which dispatches into auth.so; auth.so's body
 * then calls call_get_session_user which dispatches back into auth.so.
 *
 * The key property: module A has its own static NDX_DECL adapter for
 * ptr_lookup (no .call, hook_id==-1), and lookup resolution happens on
 * first invocation inside libndx's hot path.
 */
#include "ptr_args.h"
#include "../../src/papi.h"
#include <string.h>

ndx_t ndx;

/* ptr_resolve is this module's own NDX_DEF — but invokes ptr_lookup
 * from module B via its own NDX_DECL adapter (ptr_lookup_adapter here
 * is a distinct static instance from the one in mod_ptr_args.so). */
NDX_DEF(const char *, ptr_resolve_remote, int, which)
{
	const char *key;
	switch (which) {
		case 1: key = "tok-alice"; break;
		case 2: key = "tok-bob";   break;
		case 3: key = "tok-carol"; break;
		default: return NULL;
	}
	return call_ptr_lookup(key);
}

MODULE_API void ndx_install(void) {}

MODULE_API ndx_t* get_ndx_ptr(void) {
	return &ndx;
}

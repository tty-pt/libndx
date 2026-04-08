#ifndef NDX_MOD_H
#define NDX_MOD_H

/* Define the guard before including ndx.h so the default NULL definition
 * in ndx.h is suppressed; we provide our own that is never used directly. */
#define __NDX_CALLER_PATH_DEFINED__

#include "ndx.h"
static struct ndx_ctx ndx;

struct ndx_ctx;

MODULE_API struct ndx_ctx *
get_ndx_ptr(void)
{
	return &ndx;
}

/* In module context, NDX_CALL routes through the injected ndx context so
 * that (a) the caller identity is always ndx.module_path (set by the host
 * at load time, forgery-proof) and (b) dispatch goes through ndx.call
 * (host-injectable). This overrides the bare-symbol version in ndx.h. */
#undef NDX_CALL
#define NDX_CALL(retp, fname, ...) { \
	struct fname##_args args = { __VA_ARGS__ }; \
	ndx.set_caller(ndx.module_path); \
	ndx.call(retp, XSTR(fname), &args); \
}

/**
 * @brief Pledge exclusive call rights to a hook from this module.
 *
 * Sets the caller identity and records that only this module may invoke
 * the named hook via ndx_call. First caller wins; subsequent pledges for
 * the same hook return NDX_ERR_EPERM. Any other module calling a pledged
 * hook receives NDX_ERR_EPERM.
 *
 * Must be called from ndx_install().
 *
 * @param hook_name String name of the hook to pledge (e.g., "get_counter")
 */
#define ndx_pledge(hook_name) \
	(ndx.set_caller(ndx.module_path), ndx.pledge(hook_name))

#endif

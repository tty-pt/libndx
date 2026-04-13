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
 * at load time, forgery-proof), (b) dispatch goes through ndx.call
 * (host-injectable), and (c) the module's assigned region is used. */
#undef NDX_CALL
#define NDX_CALL(retp, fname, ...) { \
	struct fname##_args args = { __VA_ARGS__ }; \
	ndx.set_caller(ndx.module_path); \
	ndx.call(retp, XSTR(fname), &args); \
}

/* Module-context __ndx_caller_path__ for any macro that needs it */
static UNUSED const char *__ndx_caller_path__ = NULL; /* unused in mod ctx */

/**
 * @brief Load a module into the caller's current region.
 *
 * Sets caller identity then forwards to the host's ndx_load.
 *
 * @param fname     Path to .so / .dll
 */
#define ndx_load(fname) \
	ndx_mod_load_(&ndx, (fname))
static inline UNUSED int
ndx_mod_load_(struct ndx_ctx *_n, char *f)
{ _n->set_caller(_n->module_path); return _n->load(f); }

/**
 * @brief Pledge exclusive call rights to a hook, scoped to the caller's region.
 *
 * Sets the caller identity and records that only this module may invoke
 * the named hook via ndx_call within this module's region.
 * First caller wins; subsequent pledges for the same hook in the same
 * region return NDX_ERR_EPERM.
 *
 * Must be called from ndx_install().
 *
 * @param hook_name String name of the hook to pledge (e.g., "get_counter")
 */
#define ndx_pledge(hook_name) \
	ndx_mod_pledge_(&ndx, (hook_name))
static inline UNUSED int
ndx_mod_pledge_(struct ndx_ctx *_n, const char *h)
{ _n->set_caller(_n->module_path); return _n->pledge(h); }

/**
 * @brief Deny a hook or module within the caller's current region.
 *
 * The deny applies to sub-regions (children-only semantics).
 *
 * @param what       Hook name (NDX_DENY_HOOK) or module path (NDX_DENY_MODULE)
 * @param type       NDX_DENY_HOOK or NDX_DENY_MODULE
 */
#define ndx_deny(what, type) \
	ndx_mod_deny_(&ndx, (what), (type))
static inline UNUSED int
ndx_mod_deny_(struct ndx_ctx *_n, const char *w, ndx_deny_type_t t)
{ _n->set_caller(_n->module_path); return _n->deny(w, t); }

/**
 * @brief Register a middleware interceptor for @p hook_name in the caller's
 * current region.
 *
 * @param hook_name  Hook name to intercept
 * @param fn         Interceptor function pointer (ndx_interceptor_fn_t*)
 * @param ud         User data passed to fn
 */
#define ndx_intercept(hook_name, fn, ud) \
	ndx_mod_intercept_(&ndx, (hook_name), (fn), (ud))
static inline UNUSED int
ndx_mod_intercept_(struct ndx_ctx *_n, const char *h,
                   ndx_interceptor_fn_t *fn, void *ud)
{ _n->set_caller(_n->module_path); return _n->intercept(h, fn, ud); }

/**
 * @brief Claim a sub-region of @p bits width under the caller's current region.
 *
 * Only valid inside ndx_install().
 * The parent region's claim handler (if any) is invoked first.
 *
 * @param bits  Requested child region width in bits (1-58).
 */
#define ndx_claim(bits) \
	ndx_mod_claim_(&ndx, (bits))
static inline UNUSED int
ndx_mod_claim_(struct ndx_ctx *_n, uint8_t bits)
{ _n->set_caller(_n->module_path); return _n->claim(bits); }

/**
 * @brief Register a claim handler on the caller's current region.
 *
 * @param fn  Claim handler function
 * @param ud  User data passed to fn
 */
#define ndx_on_claim(fn, ud) \
	ndx_mod_on_claim_(&ndx, (fn), (ud))
static inline UNUSED int
ndx_mod_on_claim_(struct ndx_ctx *_n, ndx_claim_handler_fn_t *fn, void *ud)
{ _n->set_caller(_n->module_path); return _n->on_claim(fn, ud); }

/**
 * @brief Enumerate immediate child regions of the caller's current region.
 *
 * @param fn  Callback: fn(child_id, ud). Return NDX_OK to continue.
 * @param ud  User data passed to fn
 */
#define ndx_region_each(fn, ud) \
	ndx_mod_region_each_(&ndx, (fn), (ud))
static inline UNUSED int
ndx_mod_region_each_(struct ndx_ctx *_n, ndx_region_each_fn_t *fn, void *ud)
{ _n->set_caller(_n->module_path); return _n->region_each(fn, ud); }

/**
 * @brief Return the region ID assigned to this module (diagnostic/internal).
 */
#define ndx_my_region() (ndx.region_id)

#endif

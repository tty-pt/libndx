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
	ndx.call(retp, &fname##_adapter, &args, ndx.module_path); \
}

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
 * The deny applies to the caller's current region and all its descendants.
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
 * @brief Set or clear the claim gate on the caller's current region.
 *
 * Non-NULL fn: enables the gate — subsequent ndx_load() calls require an
 * ndx_claim symbol from the module.
 * NULL fn: clears the gate — subsequent ndx_load() calls need no ndx_claim.
 *
 * @param fn  Claim handler function, or NULL to clear.
 * @param ud  User data passed to fn (ignored when fn is NULL).
 */
#define ndx_require_claim(fn, ud) \
	ndx_mod_require_claim_(&ndx, (fn), (ud))
static inline UNUSED int
ndx_mod_require_claim_(struct ndx_ctx *_n, ndx_claim_handler_fn_t *fn, void *ud)
{ _n->set_caller(_n->module_path); return _n->require_claim(fn, ud); }

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
 * @brief Unload a module from the caller's current region.
 *
 * @param fname  Path passed to ndx_load() (without .so/.dll suffix)
 */
#define ndx_unload(fname) \
	ndx_mod_unload_(&ndx, (fname))
static inline UNUSED int
ndx_mod_unload_(struct ndx_ctx *_n, char *f)
{ _n->set_caller(_n->module_path); return _n->unload(f); }

/**
 * @brief Reload a module in place in the caller's current region.
 *
 * @param fname  Path passed to ndx_load() (without .so/.dll suffix)
 */
#define ndx_reload(fname) \
	ndx_mod_reload_(&ndx, (fname))
static inline UNUSED int
ndx_mod_reload_(struct ndx_ctx *_n, char *f)
{ _n->set_caller(_n->module_path); return _n->reload(f); }

/**
 * @brief Return the region ID assigned to this module (diagnostic/internal).
 */
#define ndx_my_region() (ndx.region_id)

/* -------------------------------------------------------------------------
 * Per-region module state helpers
 *
 * Usage:
 *
 *   NDX_REGION_STATE {
 *       int    counter;
 *       char  *label;
 *   };
 *   NDX_REGION_INIT;
 *
 *   // In any hook or ndx_install:
 *   NDX_RS->counter++;
 *
 * NDX_REGION_STATE  — begins the struct definition (struct ndx_rs_s { ... })
 * NDX_REGION_INIT   — emits ndx_region_state_size() after the struct closing
 *                     brace; place on the line after the closing brace.
 * NDX_RS            — typed pointer to this module's current region state.
 *                     Valid only during hook dispatch or ndx_install.
 * ------------------------------------------------------------------------- */

/** Begin per-region state struct declaration. */
#define NDX_REGION_STATE struct ndx_rs_s

/** Emit the ndx_region_state_size() export after the struct definition. */
#define NDX_REGION_INIT \
	MODULE_API size_t ndx_region_state_size(void) \
	{ return sizeof(struct ndx_rs_s); }

/** Typed pointer to the current region's state block. */
#define NDX_RS ((struct ndx_rs_s *)ndx.region_state)

#endif

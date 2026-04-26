#ifndef NDX_MOD_H
#define NDX_MOD_H

#include "ndx.h"
static struct ndx_ctx ndx;

struct ndx_ctx;

MODULE_API struct ndx_ctx *
get_ndx_ptr(void)
{
	return &ndx;
}

/* In module context, NDX_CALL routes through the injected ndx context so
 * dispatch goes through ndx.call and uses the module's assigned region. */
#undef NDX_CALL
#define NDX_CALL(retp, fname, ...) { \
	struct fname##_args args = { __VA_ARGS__ }; \
	ndx.call(retp, &fname##_adapter, &args); \
}

/**
 * @brief Load a module into the caller's current region.
 *
 * @param fname     Path to .so / .dll
 */
#define ndx_load(fname) \
	ndx_mod_load_(&ndx, (fname))
static inline UNUSED int
ndx_mod_load_(struct ndx_ctx *_n, char *f)
{ return _n->load(f); }

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
{ return _n->deny(w, t); }

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
{ return _n->require_claim(fn, ud); }

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
{ return _n->region_each(fn, ud); }

/**
 * @brief Run @p fn with the caller's current region temporarily set to
 * @p region_id.
 *
 * Nested hook calls inside @p fn inherit that region.
 */
#define ndx_with_region(region_id, fn, ud) \
	ndx_mod_with_region_(&ndx, (region_id), (fn), (ud))
static inline UNUSED int
ndx_mod_with_region_(struct ndx_ctx *_n, uint64_t region_id,
                     ndx_scope_fn_t *fn, void *ud)
{ return _n->with_region(region_id, fn, ud); }

/** @brief Return the current execution region for this thread. */
#define ndx_current_region() (ndx.current_region())

/**
 * @brief Unload a module from the caller's current region.
 *
 * @param fname  Path passed to ndx_load() (without .so/.dll suffix)
 */
#define ndx_unload(fname) \
	ndx_mod_unload_(&ndx, (fname))
static inline UNUSED int
ndx_mod_unload_(struct ndx_ctx *_n, char *f)
{ return _n->unload(f); }

/**
 * @brief Reload a module in place in the caller's current region.
 *
 * @param fname  Path passed to ndx_load() (without .so/.dll suffix)
 */
#define ndx_reload(fname) \
	ndx_mod_reload_(&ndx, (fname))
static inline UNUSED int
ndx_mod_reload_(struct ndx_ctx *_n, char *f)
{ return _n->reload(f); }

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

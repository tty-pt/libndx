#ifndef XY_MOD_H
#define XY_MOD_H

#include "xy.h"
static struct xy_ctx xy;

struct xy_ctx;

XY_MODULE_API __attribute__((weak)) struct xy_ctx *
get_xy_ptr(void)
{
	return &xy;
}

/* In module context, XY_CALL routes through the injected xy context so
 * dispatch goes through xy.call and uses the module's assigned region. */
#undef XY_CALL
#define XY_CALL(retp, fname, ...) { \
	struct fname##_args args = { __VA_ARGS__ }; \
	xy.call(retp, &fname##_adapter, &args); \
}

/**
 * @brief Load a module into the caller's current region.
 *
 * @param fname     Path to .so / .dll
 */
#define xy_load(fname) \
	xy_mod_load_(&xy, (fname))
static inline UNUSED int
xy_mod_load_(struct xy_ctx *_n, char *f)
{ return _n->load(f); }

/**
 * @brief Deny a hook or module within the caller's current region.
 *
 * The deny applies to the caller's current region and all its descendants.
 *
 * @param what       Hook name (XY_DENY_HOOK) or module path (XY_DENY_MODULE)
 * @param type       XY_DENY_HOOK or XY_DENY_MODULE
 */
#define xy_deny(what, type) \
	xy_mod_deny_(&xy, (what), (type))
static inline UNUSED int
xy_mod_deny_(struct xy_ctx *_n, const char *w, xy_deny_type_t t)
{ return _n->deny(w, t); }

/**
 * @brief Set or clear the claim gate on the caller's current region.
 *
 * Non-NULL fn: enables the gate — subsequent xy_load() calls require an
 * xy_claim symbol from the module.
 * NULL fn: clears the gate — subsequent xy_load() calls need no xy_claim.
 *
 * @param fn  Claim handler function, or NULL to clear.
 * @param ud  User data passed to fn (ignored when fn is NULL).
 */
#define xy_require_claim(fn, ud) \
	xy_mod_require_claim_(&xy, (fn), (ud))
static inline UNUSED int
xy_mod_require_claim_(struct xy_ctx *_n, xy_claim_handler_fn_t *fn, void *ud)
{ return _n->require_claim(fn, ud); }

/**
 * @brief Enumerate immediate child regions of the caller's current region.
 *
 * @param fn  Callback: fn(child_id, ud). Return XY_OK to continue.
 * @param ud  User data passed to fn
 */
#define xy_region_each(fn, ud) \
	xy_mod_region_each_(&xy, (fn), (ud))
static inline UNUSED int
xy_mod_region_each_(struct xy_ctx *_n, xy_region_each_fn_t *fn, void *ud)
{ return _n->region_each(fn, ud); }

/**
 * @brief Run @p fn with the caller's current region temporarily set to
 * @p region_id.
 *
 * Nested hook calls inside @p fn inherit that region.
 */
#define xy_with_region(region_id, fn, ud) \
	xy_mod_with_region_(&xy, (region_id), (fn), (ud))
static inline UNUSED int
xy_mod_with_region_(struct xy_ctx *_n, uint64_t region_id,
                     xy_scope_fn_t *fn, void *ud)
{ return _n->with_region(region_id, fn, ud); }

/** @brief Return the current execution region for this thread. */
#define xy_current_region() (xy.current_region())

/**
 * @brief Unload a module from the caller's current region.
 *
 * @param fname  Path passed to xy_load() (without .so/.dll suffix)
 */
#define xy_unload(fname) \
	xy_mod_unload_(&xy, (fname))
static inline UNUSED int
xy_mod_unload_(struct xy_ctx *_n, char *f)
{ return _n->unload(f); }

/**
 * @brief Reload a module in place in the caller's current region.
 *
 * @param fname  Path passed to xy_load() (without .so/.dll suffix)
 */
#define xy_reload(fname) \
	xy_mod_reload_(&xy, (fname))
static inline UNUSED int
xy_mod_reload_(struct xy_ctx *_n, char *f)
{ return _n->reload(f); }

/**
 * @brief Return the region ID assigned to this module (diagnostic/internal).
 */
#define xy_my_region() (xy.region_id)

/* -------------------------------------------------------------------------
 * Per-region module state helpers
 *
 * Usage:
 *
 *   XY_REGION_STATE {
 *       int    counter;
 *       char  *label;
 *   };
 *   XY_REGION_INIT;
 *
 *   // In any hook or xy_install:
 *   XY_RS->counter++;
 *
 * XY_REGION_STATE  — begins the struct definition (struct xy_rs_s { ... })
 * XY_REGION_INIT   — emits xy_region_state_size() after the struct closing
 *                     brace; place on the line after the closing brace.
 * XY_RS            — typed pointer to this module's current region state.
 *                     Valid only during hook dispatch or xy_install.
 * ------------------------------------------------------------------------- */

/** Begin per-region state struct declaration. */
#define XY_REGION_STATE struct xy_rs_s

/** Emit the xy_region_state_size() export after the struct definition. */
#define XY_REGION_INIT \
	XY_MODULE_API size_t xy_region_state_size(void) \
	{ return sizeof(struct xy_rs_s); }

/** Typed pointer to the current region's state block. */
#define XY_RS ((struct xy_rs_s *)xy.region_state)

#endif

/**
 * @file ndx.h
 * @brief Modding and extensibility API.
 *
 * @section ndx_synopsis Synopsis
 * @code
 * NDX_HOOK_DEF(int, on_tick, int, dt);
 * on_tick(16);
 * @endcode
 *
 * @section ndx_overview Overview
 * libndx provides a plugin/module system where:
 * - <b>Host</b> defines hooks and loads modules
 * - <b>Module</b> implements hooks in a shared library
 *
 * @section ndx_usage Usage
 * 1. Host uses NDX_HOOK_DEF to define hook signatures
 * 2. Host loads modules via ndx_load()
 * 3. Modules use NDX_LISTENER to implement hooks they provide
 * 4. Modules include ndx-mod.h
 *
 * @section ndx_deps Dependencies
 * Dependencies between modules are handled through the API:
 * - Common headers use NDX_HOOK_DECL for shared hook signatures
 * - Implementing modules use NDX_LISTENER to define hooks
 * - Dependent modules call ndx_load() in ndx_install() to load dependencies
 * - Then call hooks as normal functions from dependencies
 *
 * @section ndx_errors Error Handling
 * - Functions return NDX_OK (0) on success or negative NDX_ERR_* on failure
 * - Use ndx_errno() to get last error code
 * - Use ndx_strerror(err) to get human-readable message
 */
#ifndef NDX_H
#define NDX_H

/* RECOMMENDATIONS:
 *
 * - Avoid passing entire objects in NDX calls. It's not very
 *   useful since getting / putting things by id can be fast.
 *   Only when you really don't have another way because you
 *   modify the object in the calling function and don't put
 *   and set around the NDX_CALL. Usually mods will use their
 *   custom object types, anyway.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <ttypt/ndx-pp.h>
#include <ttypt/qsys.h>

/**
 * @brief Maximum size for return types in hooks.
 *
 * Return types larger than this will cause a compile-time static assertion
 * when using NDX_HOOK_DEF or NDX_LISTENER.
 */
#define NDX_MAX_RET_SIZE 4096

/**
 * @brief Invalid hook ID returned when lookup fails.
 */
#define NDX_INVALID ((unsigned) -1)

/**
 * @brief Mark intentionally-unused symbols or parameters.
 */
#define UNUSED __attribute__((unused))

/**
 * @brief Success return code.
 */
#define NDX_OK             0

/**
 * @brief Error: Module or hook not found.
 */
#define NDX_ERR_NOTFOUND  -1

/**
 * @brief Error: Invalid argument passed to function.
 */
#define NDX_ERR_INVALID   -2

/**
 * @brief Error: Return type exceeds NDX_MAX_RET_SIZE / no slot available.
 */
#define NDX_ERR_TOOBIG    -3

/**
 * @brief Error: Library initialization failed.
 */
#define NDX_ERR_INIT      -4

/**
 * @brief Error: Operation not permitted (pledge or claim violation).
 */
#define NDX_ERR_EPERM     -5

/**
 * @brief Adapter for dispatching hook calls to modules.
 *
 * This struct is used internally to route calls to all modules
 * that implement a given hook. Created automatically by NDX_LISTENER.
 */
typedef struct {
	/** @brief Name of the hook (e.g., "on_tick") */
	char name[64];

	/** @brief Size of the arguments struct */
	size_t arg_size;

	/** @brief Size of the return type */
	size_t ret_size;

	/** @brief Internal call dispatcher */
	void (*call)(void *, void *, void *);

	/** @brief Monotonic hook ID assigned by ndx_areg(); used for O(1) dispatch.
	 *  Zero-initialised by NDX_LISTENER; filled in by ndx_areg() on first registration. */
	int hook_id;

	/** @brief Buffer for last return value */
	char ret[NDX_MAX_RET_SIZE];

	/** @brief Number of modules that ran this hook */
	unsigned ran;
} ndx_adapter_t;

typedef int ndx_scope_fn_t(void *ud);

/**
 * @brief Module callback function type.
 *
 * Used for mod_auto_init(), ndx_install()
 */
typedef void (*mod_cb_t)(void);

#ifndef __ID_MARKER__
#define __ID_MARKER__ static
#endif

#if defined(_WIN32)
  /**
   * @brief Export symbol from module (Windows).
   */
  #define MODULE_API __declspec(dllexport)
#else
  /**
   * @brief Export symbol from module (POSIX).
   */
  #define MODULE_API __attribute__((visibility("default")))
#endif

#if defined(__APPLE__)
  /**
   * @brief Mach-O equivalent of .init_array for macOS.
   */
  #define AUTO_INIT __attribute__((used)) __attribute__((section("__DATA,__mod_init_func")))
#else
  /**
   * @brief ELF section for initialization pointers.
   */
  #define AUTO_INIT __attribute__((used)) __attribute__((section(".init_array")))
#endif

#ifdef __NDX_CALLER_PATH_DEFINED__
#define __NDX_HOOK_DISPATCH(retp, adapterp, argp) \
	ndx.call((retp), (adapterp), (argp), ndx.module_path)
#else
#define __NDX_HOOK_DISPATCH(retp, adapterp, argp) \
	ndx_call((retp), (adapterp), (argp), __ndx_caller_path__)
#endif

/**
 * @brief Declare a hook that can be called like a normal function.
 *
 * This is the preferred declaration form for call sites that should read as
 * ordinary C. The generated inline function still routes through NDX_CALL, so
 * caller identity and the current execution region are preserved.
 *
 * Do not use this macro in the same translation unit that defines a listener
 * for the same hook name with NDX_LISTENER; the listener should keep using
 * NDX_LISTENER and dispatch recursively with NDX_CALL when needed.
 */
#define NDX_HOOK_DECL(ftype, fname, ...) \
	typedef ftype fname##_t(NDX_FA(__VA_ARGS__)); \
	struct fname##_args { \
		NDX_PG(__VA_ARGS__) \
	}; \
	static ndx_adapter_t __ndx_decl_##fname##_adapter = { \
		.name = XSTR(fname), \
		.arg_size = sizeof(struct fname##_args), \
		.ret_size = sizeof(ftype), \
		.hook_id = -1, \
	}; \
	static inline UNUSED \
	ftype fname(NDX_FA(__VA_ARGS__)) { \
		ftype ret; \
		memset(&ret, 0, sizeof(ret)); \
		struct fname##_args args = { NDX_DA(__VA_ARGS__) }; \
		__NDX_HOOK_DISPATCH(&ret, &__ndx_decl_##fname##_adapter, &args); \
		return ret; \
	} \
	typedef int __ndx_hook_decl_##fname##_semicolon_eater_t

/**
 * @brief Define the canonical adapter for a normal-function hook.
 *
 * Use NDX_HOOK_DECL in shared headers; use NDX_HOOK_DEF in the one
 * translation unit that emits the canonical adapter. NDX_HOOK_DEF is
 * self-sufficient in definition TUs and also provides the ordinary call
 * syntax there.
 */
#define NDX_HOOK_DEF(ftype, fname, ...) \
	typedef ftype fname##_t(NDX_FA(__VA_ARGS__)); \
	struct fname##_args { \
		NDX_PG(__VA_ARGS__) \
	}; \
	static ndx_adapter_t __ndx_decl_##fname##_adapter = { \
		.name = XSTR(fname), \
		.arg_size = sizeof(struct fname##_args), \
		.ret_size = sizeof(ftype), \
		.hook_id = -1, \
	}; \
	static inline UNUSED \
	ftype fname(NDX_FA(__VA_ARGS__)) { \
		ftype ret; \
		memset(&ret, 0, sizeof(ret)); \
		struct fname##_args args = { NDX_DA(__VA_ARGS__) }; \
		__NDX_HOOK_DISPATCH(&ret, &__ndx_decl_##fname##_adapter, &args); \
		return ret; \
	} \
	_Static_assert(sizeof(ftype) <= NDX_MAX_RET_SIZE, \
		"return type too large for ndx_adapter_t"); \
	void fname##_adapter_call(void *res, void *fn, void *arg) { \
		fname##_t *cast_fn; \
		if (!fn) { \
			WARN("%s_adapter_call: '%s' wasn't defined\n", \
				XSTR(fname), XSTR(fname)); \
			return; \
		} \
		* (void **) &cast_fn = fn; \
		struct fname##_args *__ndx_a = arg; (void)__ndx_a; \
		ftype result = cast_fn(NDX_NP(__VA_ARGS__)); \
		if (res) *(ftype *)res = result; \
	} \
	ndx_adapter_t fname##_adapter  __attribute__((visibility("default"))) = { \
		.name = XSTR(fname), \
		.arg_size = sizeof(struct fname##_args), \
		.ret_size = sizeof(ftype), \
		.call = &fname##_adapter_call, \
		.hook_id = -1, \
	}; \
	void fname##_adapter_reg(void) { \
		ndx_areg(XSTR(fname), &fname##_adapter); \
	} \
	AUTO_INIT \
	void (*fname##_adapter_reg_p)(void) = fname##_adapter_reg

/**
 * @brief Define a module listener implementation for a hook.
 *
 * Use this in modules that implement hooks. The host dispatches to these
 * listener functions when the matching hook is called.
 *
 * Creates:
 * - \c fname##_t: Function typedef
 * - \c fname: Function implementation (you provide this)
 * - \c fname##_id: Hook ID (set on registration)
 *
 * The hook is auto-registered when the module loads.
 *
 * @param ftype Return type (e.g., int, void)
 * @param fname Function name (e.g., on_tick)
 * @param ... Argument types and names (e.g., int, dt)
 *
 * Example:
 * @code
 * NDX_LISTENER(int, on_tick, int, dt);
 * int on_tick(int dt) { return dt + 1; }
 * @endcode
 */
#define NDX_LISTENER(ftype, fname, ...) \
	typedef ftype fname##_t(NDX_FA(__VA_ARGS__)); \
	fname##_t fname; \
	struct fname##_args { \
		NDX_PG(__VA_ARGS__) \
	}; \
	extern ndx_adapter_t fname##_adapter; \
	_Static_assert(sizeof(ftype) <= NDX_MAX_RET_SIZE, \
		"return type too large for ndx_adapter_t"); \
	fname##_t fname; \
	void fname##_adapter_call(void *res, void *fn, void *arg) { \
	fname##_t *cast_fn; \
	if (!fn) { \
		WARN("%s_adapter_call: '%s' wasn't defined\n", \
				XSTR(fname), XSTR(fname)); \
		return; \
	} \
	* (void **) &cast_fn = fn; \
	struct fname##_args *__ndx_a = arg; (void)__ndx_a; \
	ftype result = cast_fn(NDX_NP(__VA_ARGS__)); \
	if (res) *(ftype *)res = result; \
	} \
	ndx_adapter_t fname##_adapter  __attribute__((visibility("default"))) = { \
		.name = XSTR(fname), \
		.arg_size = sizeof(struct fname##_args), \
		.ret_size = sizeof(ftype), \
		.call = &fname##_adapter_call, \
		.hook_id = -1, \
	}; \
	void fname##_adapter_reg(void) { \
		ndx_areg(XSTR(fname), &fname##_adapter); \
	} \
	AUTO_INIT \
	void (*fname##_adapter_reg_p)(void) = fname##_adapter_reg; \
	ftype fname(NDX_FA(__VA_ARGS__))

/**
 * @brief Call a mod hook by name within the current thread-local region.
 *
 * This macro calls all modules that implement the named hook whose region
 * is a descendant-or-equal of the caller's current region.
 * The return value is from the last module that ran.
 *
 * @param retp Pointer to store return value
 * @param fname Hook function name
 * @param ... Arguments to pass to the hook
 *
 * Example:
 * @code
 * int result;
 * NDX_CALL(&result, on_tick, 16);
 * @endcode
 */
/* the caller uses this to call */
#define NDX_CALL(retp, fname, ...) { \
	struct fname##_args args = { __VA_ARGS__ }; \
	ndx_call(retp, &fname##_adapter, &args, __ndx_caller_path__); \
}

/* Internal types - used by ndx_t struct */
typedef unsigned ndx_areg_t(char *name, ndx_adapter_t *adapter);
typedef int ndx_call_t(void *retp, ndx_adapter_t *adapter, void *args, const char *caller);
typedef int ndx_last_t(void *ret);
typedef void ndx_set_caller_t(const char *module_path);

/**
 * @brief Pledge exclusive call rights to a hook, scoped to the caller's region.
 *
 * In module context the pledge is scoped to the module's assigned region.
 * In host context (caller == NULL / root) the pledge is root-scoped (global).
 *
 * The first caller wins; any subsequent ndx_pledge for the same hook in the
 * same region returns NDX_ERR_EPERM.  After a pledge is recorded, any other
 * caller invoking the pledged hook via ndx_call in that region will receive
 * NDX_ERR_EPERM.
 *
 * @param hook_name Name of the hook to restrict (e.g., "get_counter")
 * @return NDX_OK on success, NDX_ERR_EPERM if already pledged,
 *         NDX_ERR_INVALID if caller identity is unavailable
 */
typedef int ndx_pledge_t(const char *hook_name);

/* -------------------------------------------------------------------------
 * Region API types
 * ------------------------------------------------------------------------- */

/** Root region — ancestor of all regions.  Passing this to ndx_call
 *  dispatches to every module regardless of its region. */
#define NDX_REGION_ROOT ((uint64_t)0)

/** Sentinel returned when region allocation fails. */
#define NDX_REGION_INVALID ((uint64_t)-1)

/**
 * @brief Deny target (hook name or module path) selector.
 */
typedef enum {
	NDX_DENY_HOOK   = 0, /**< @p what is a hook name */
	NDX_DENY_MODULE = 1, /**< @p what is a module path */
} ndx_deny_type_t;

/**
 * @brief Deny a hook or module within the caller's current region.
 *
 * The deny applies to sub-regions only (children-only semantics by default).
 * A module's own region is never blocked by its own deny.
 *
 * @param what  Hook name (NDX_DENY_HOOK) or module path (NDX_DENY_MODULE).
 * @param type  NDX_DENY_HOOK or NDX_DENY_MODULE.
 * @return NDX_OK or a negative NDX_ERR_* code.
 */
typedef int ndx_deny_t(const char *what, ndx_deny_type_t type);

/**
 * @brief Interceptor function type (middleware pattern).
 *
 * @param hook    Name of the hook being dispatched
 * @param args    Packed argument struct (cast to the hook's args type)
 * @param ret     Return-value buffer (cast to the hook's return type)
 * @param next    Call this to continue the chain; skip it to block
 * @param next_ud Opaque pointer to pass verbatim to @p next (do not modify)
 * @param ud      User-supplied data from ndx_intercept()
 * @return NDX_OK, or a negative error code to signal failure upstream
 */
typedef void (*ndx_call_fn_t)(void *args, void *ret, void *ud);
typedef int ndx_interceptor_fn_t(
	const char *hook, void *args, void *ret,
	ndx_call_fn_t next, void *next_ud, void *ud);

/**
 * @brief Register a middleware interceptor for @p hook_name in the caller's
 * current region.
 *
 * Interceptors are called outermost-first (root → target region).  Each
 * interceptor may inspect/modify args and ret, call @p next to continue, or
 * return early to block.
 */
typedef int ndx_intercept_t(const char *hook_name,
                             ndx_interceptor_fn_t *fn, void *ud);

/**
 * @brief Claim handler type.
 *
 * @param module_path    Path of the child module making the request (read-only)
 * @param requested_bits Number of bits the child asked for
 * @param granted_bits   Out-param: set to the approved width
 * @param ud             User data from ndx_require_claim()
 * @return NDX_OK to approve (with *granted_bits set), NDX_ERR_EPERM to reject.
 */
typedef int ndx_claim_handler_fn_t(const char *module_path,
                                    uint8_t     requested_bits,
                                    uint8_t    *granted_bits,
                                    void       *ud);

/**
 * @brief Set or clear the claim gate on the caller's current region.
 *
 * When @p fn is non-NULL, the gate is opened: all subsequent ndx_load() calls
 * into this region require the module to export a `MODULE_API uint8_t ndx_claim`
 * data symbol.  If the symbol is absent, ndx_load() returns NDX_ERR_EPERM and
 * ndx_install() never runs.  If the symbol is present, the host performs the
 * claim on behalf of the module by invoking @p fn before running ndx_install().
 *
 * Modules loaded *before* this call in the same ndx_install() context are
 * unaffected — they load flat into the current region as normal.
 *
 * When @p fn is NULL, the gate is cleared: subsequent ndx_load() calls no
 * longer require an ndx_claim symbol.  A later non-NULL call re-enables the
 * gate.
 *
 * @param fn  Claim handler function, or NULL to clear the claim gate.
 * @param ud  User data passed to fn (ignored when fn is NULL).
 * @return NDX_OK or negative NDX_ERR_* code.
 */
typedef int ndx_require_claim_t(ndx_claim_handler_fn_t *fn, void *ud);

/**
 * @brief Enumerate immediate child regions of the caller's current region.
 *
 * Calls @p fn(child_id, ud) for each child in allocation order.
 * child_id is an opaque uint64_t region identifier.
 *
 * Return NDX_OK from @p fn to continue, any other value to stop.
 * ndx_region_each returns the last value returned by @p fn, or NDX_OK if
 * there were no children.
 */
typedef int ndx_region_each_fn_t(uint64_t child_id, void *ud);
typedef int ndx_region_each_t(ndx_region_each_fn_t *fn, void *ud);
typedef int ndx_with_region_t(uint64_t region_id, ndx_scope_fn_t *fn, void *ud);
typedef uint64_t ndx_current_region_t(void);

ndx_areg_t   ndx_areg;
ndx_call_t   ndx_call;
ndx_last_t   ndx_last;
ndx_pledge_t ndx_pledge;
ndx_deny_t           ndx_deny;
ndx_intercept_t      ndx_intercept;
ndx_require_claim_t  ndx_require_claim;
ndx_region_each_t    ndx_region_each;
ndx_with_region_t    ndx_with_region;
ndx_current_region_t ndx_current_region;

/**
 * @brief Load or reload a module into the caller's current region.
 *
 * @param fname     Path to .so (Linux) or .dll (Windows) file
 * @return NDX_OK on success, negative error code on failure
 *
 * On first load, calls ndx_install().
 * Subsequent loads of the same path into the same region are no-ops.
 */
typedef int ndx_load_t(char *fname);
ndx_load_t ndx_load;

/**
 * @brief Unload a previously loaded module from the caller's current region.
 *
 * Decrements the module's reference count.  When the count reaches zero:
 *  - Calls the module's ndx_uninstall() export (if present); if it returns
 *    non-zero the unload is aborted and NDX_ERR_EPERM is returned.
 *  - Recursively unloads child modules whose reference count would reach zero.
 *  - Removes deny/pledge/intercept entries registered by the module.
 *  - Invalidates cached function pointers across all other modules.
 *  - Calls dlclose() and frees internal bookkeeping.
 *
 * @param fname  Path that was passed to ndx_load() (without .so/.dll suffix)
 * @return NDX_OK on success, NDX_ERR_NOTFOUND if not loaded, NDX_ERR_EPERM if
 *         ndx_uninstall() vetoed the unload.
 */
typedef int ndx_unload_t(char *fname);
ndx_unload_t ndx_unload;

/**
 * @brief Reload a module in place, preserving its dispatch position.
 *
 * Equivalent to ndx_unload() followed by ndx_load() in the same region, but
 * the reloaded module is re-inserted at its original position in the dispatch
 * order rather than appended at the tail.
 *
 * If ndx_uninstall() vetoes the unload the reload is aborted.
 *
 * @param fname  Path that was passed to ndx_load() (without .so/.dll suffix)
 * @return NDX_OK on success, negative NDX_ERR_* on failure.
 */
typedef int ndx_reload_t(char *fname);
ndx_reload_t ndx_reload;

/**
 * @brief Shutdown and unload all modules.
 *
 * Calls dlclose() on all loaded module handles.
 * After this, ndx_load() can be called again.
 */
typedef void ndx_shutdown_t(void);
ndx_shutdown_t ndx_shutdown;

/**
 * @brief Get last error code.
 *
 * @return Last error code from failed API call
 *
 * Most API functions set this on failure.
 */
typedef int ndx_errno_t(void);
ndx_errno_t ndx_errno;

/**
 * @brief Get human-readable error message.
 *
 * @param err Error code (e.g., from ndx_errno())
 * @return Static string describing the error
 */
typedef const char *ndx_strerror_t(int err);
ndx_strerror_t ndx_strerror;

void ndx_init(void);

/**
 * @brief Set the current caller identity for pledge enforcement.
 *
 * Called automatically by the NDX_CALL macro. Not intended for direct use.
 */
ndx_set_caller_t ndx_set_caller;

/**
 * @brief Per-translation-unit caller path for pledge enforcement.
 *
 * Defaults to NULL (unrestricted host context). Overridden in ndx-mod.h.
 * The NDX_CALL macro passes this to ndx_set_caller before each dispatch.
 */
#ifndef __NDX_CALLER_PATH_DEFINED__
static UNUSED const char *__ndx_caller_path__ = NULL;
#endif

struct ndx_ctx {
	ndx_call_t       *call;
	ndx_areg_t       *areg;
	ndx_load_t       *load;
	ndx_errno_t      *err;
	ndx_strerror_t   *strerror;
	ndx_adapter_t    *adapter;
	ndx_last_t       *last;
	ndx_shutdown_t   *shutdown;
	/** @brief Path this module was loaded from; set by host, read-only to module */
	const char       *module_path;
	/** @brief Pledge exclusive call rights to a hook (scoped to caller's region) */
	ndx_pledge_t     *pledge;
	/** @brief Internal: set caller identity before dispatch */
	ndx_set_caller_t *set_caller;
	/** @brief Region ID assigned to this module at load time */
	uint64_t          region_id;
	/* region management API */
	ndx_deny_t            *deny;
	ndx_intercept_t       *intercept;
	ndx_require_claim_t   *require_claim;
	ndx_region_each_t     *region_each;
	ndx_with_region_t     *with_region;
	ndx_current_region_t  *current_region;
	/* unload / reload */
	ndx_unload_t          *unload;
	ndx_reload_t          *reload;
	/** @brief Per-region module state pointer; set by framework before each
	 *  hook dispatch.  NULL if the module did not export ndx_region_state_size. */
	void                  *region_state;
};
#endif

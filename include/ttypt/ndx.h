/**
 * @file ndx.h
 * @brief Modding and extensibility API.
 *
 * @section ndx_synopsis Synopsis
 * @code
 * NDX_DECL(int, on_tick, int, dt);
 * NDX_DEF(int, on_tick, int, dt);
 * call_on_tick(16);
 * @endcode
 *
 * @section ndx_overview Overview
 * libndx provides a plugin/module system where:
 * - <b>Host</b> defines hooks and loads modules
 * - <b>Module</b> implements hooks in a shared library
 *
 * @section ndx_usage Usage
 * 1. Host uses NDX_DEF to define hook signatures
 * 2. Host loads modules via ndx_load()
 * 3. Modules use NDX_DEF to implement hooks they provide
 * 4. Modules include ndx-mod.h
 *
 * @section ndx_deps Dependencies
 * Dependencies between modules are handled through the API:
 * - Common headers use NDX_DECL for shared hook signatures
 * - Implementing modules use NDX_DEF to define hooks
 * - Dependent modules call ndx_load() in ndx_install() to load dependencies
 * - Then use call_*() macros to invoke hooks from dependencies
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
#include <string.h>
#include <ttypt/qsys.h>

/**
 * @brief Maximum size for return types in hooks.
 *
 * Return types larger than this will cause a compile-time static assertion
 * when using NDX_DEF.
 */
#define NDX_MAX_RET_SIZE 4096

/**
 * @brief Weak symbol attribute for function declarations.
 *
 * Used in NDX_DECL to allow function to be optionally defined.
 */
#define WEAK __attribute__((weak))

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
 * @brief Error: Return type exceeds NDX_MAX_RET_SIZE.
 */
#define NDX_ERR_TOOBIG    -3

/**
 * @brief Error: Library initialization failed.
 */
#define NDX_ERR_INIT      -4

/**
 * @brief Adapter for dispatching hook calls to modules.
 *
 * This struct is used internally to route calls to all modules
 * that implement a given hook. Created automatically by NDX_DEF.
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

	/** @brief Buffer for last return value */
	char ret[NDX_MAX_RET_SIZE];

	/** @brief Number of modules that ran this hook */
	unsigned ran;
} ndx_adapter_t;

#define CAT(a, ...) PRIMITIVE_CAT(a, __VA_ARGS__)
#define PRIMITIVE_CAT(a, ...) a ## __VA_ARGS__

#define NDX_PC(...) \
			 PP_NARG_(__VA_ARGS__, PAIR_RSEQ_N())
#define PP_NARG_(...) \
	PP_ARG_N(__VA_ARGS__)

#define PP_ARG_N( \
		 _1,  _2,  _3,  _4,  _5,  _6,  _7,  _8, \
			_9, _10, _11, _12, _13, _14, _15, _16, \
		_17, _18, _19, _20, _21, _22, _23, _24, \
			_25, _26, _27, _28, _29, _30, _31, _32, \
		_33, _34, _35, _36, _37, _38, _39, _40, \
		_41, _42, _43, _44, _45, _46, _47, _48, \
		_49, _50, _51, _52, _53, _54, _55, _56, \
		_57, _58, _59, _60, _61, _62, _63, N, ...) N

#define PAIR_RSEQ_N() \
	31,31,30,30,29,29,28,28,27,27,26,26,25,25, \
	24,24,23,23,22,22,21,21,20,20,19,19,18,18, \
	17,17,16,16,15,15,14,14,13,13,12,12,11,11, \
	10,10, 9, 9, 8, 8, 7, 7, 6, 6, 5, 5, 4, 4, \
	 3, 3, 2, 2, 1, 1, 0, 0

#define NDX_FA(...) CAT(NDX_FA_, \
		NDX_PC(__VA_ARGS__))( __VA_ARGS__)

#define NDX_FA_1(a, b)        a b
#define NDX_FA_2(a, b, ...)   a b, NDX_FA_1(__VA_ARGS__)
#define NDX_FA_3(a, b, ...)   a b, NDX_FA_2(__VA_ARGS__)
#define NDX_FA_4(a, b, ...)   a b, NDX_FA_3(__VA_ARGS__)
#define NDX_FA_5(a, b, ...)   a b, NDX_FA_4(__VA_ARGS__)
#define NDX_FA_6(a, b, ...)   a b, NDX_FA_5(__VA_ARGS__)
#define NDX_FA_7(a, b, ...)   a b, NDX_FA_6(__VA_ARGS__)
#define NDX_FA_8(a, b, ...)   a b, NDX_FA_7(__VA_ARGS__)
#define NDX_FA_9(a, b, ...)   a b, NDX_FA_8(__VA_ARGS__)
#define NDX_FA_10(a, b, ...)  a b, NDX_FA_9(__VA_ARGS__)
#define NDX_FA_11(a, b, ...)  a b, NDX_FA_10(__VA_ARGS__)
#define NDX_FA_12(a, b, ...)  a b, NDX_FA_11(__VA_ARGS__)
#define NDX_FA_13(a, b, ...)  a b, NDX_FA_12(__VA_ARGS__)
#define NDX_FA_14(a, b, ...)  a b, NDX_FA_13(__VA_ARGS__)
#define NDX_FA_15(a, b, ...)  a b, NDX_FA_14(__VA_ARGS__)
#define NDX_FA_16(a, b, ...)  a b, NDX_FA_15(__VA_ARGS__)

#define NDX_PG(...) CAT(NDX_PG_, \
		NDX_PC(__VA_ARGS__))( __VA_ARGS__)

#define NDX_PG_1(a, b)        a b;
#define NDX_PG_2(a, b, ...)   a b; NDX_PG_1(__VA_ARGS__)
#define NDX_PG_3(a, b, ...)   a b; NDX_PG_2(__VA_ARGS__)
#define NDX_PG_4(a, b, ...)   a b; NDX_PG_3(__VA_ARGS__)
#define NDX_PG_5(a, b, ...)   a b; NDX_PG_4(__VA_ARGS__)
#define NDX_PG_6(a, b, ...)   a b; NDX_PG_5(__VA_ARGS__)
#define NDX_PG_7(a, b, ...)   a b; NDX_PG_6(__VA_ARGS__)
#define NDX_PG_8(a, b, ...)   a b; NDX_PG_7(__VA_ARGS__)
#define NDX_PG_9(a, b, ...)   a b; NDX_PG_8(__VA_ARGS__)
#define NDX_PG_10(a, b, ...)  a b; NDX_PG_9(__VA_ARGS__)
#define NDX_PG_11(a, b, ...)  a b; NDX_PG_10(__VA_ARGS__)
#define NDX_PG_12(a, b, ...)  a b; NDX_PG_11(__VA_ARGS__)
#define NDX_PG_13(a, b, ...)  a b; NDX_PG_12(__VA_ARGS__)
#define NDX_PG_14(a, b, ...)  a b; NDX_PG_13(__VA_ARGS__)
#define NDX_PG_15(a, b, ...)  a b; NDX_PG_14(__VA_ARGS__)
#define NDX_PG_16(a, b, ...)  a b; NDX_PG_15(__VA_ARGS__)

#define NDX_NA(...) CAT(NDX_NA_, \
		NDX_PC(__VA_ARGS__))( __VA_ARGS__)

#define NDX_NA_1(a, b)        args.b
#define NDX_NA_2(a, b, ...)   args.b, NDX_NA_1(__VA_ARGS__)
#define NDX_NA_3(a, b, ...)   args.b, NDX_NA_2(__VA_ARGS__)
#define NDX_NA_4(a, b, ...)   args.b, NDX_NA_3(__VA_ARGS__)
#define NDX_NA_5(a, b, ...)   args.b, NDX_NA_4(__VA_ARGS__)
#define NDX_NA_6(a, b, ...)   args.b, NDX_NA_5(__VA_ARGS__)
#define NDX_NA_7(a, b, ...)   args.b, NDX_NA_6(__VA_ARGS__)
#define NDX_NA_8(a, b, ...)   args.b, NDX_NA_7(__VA_ARGS__)
#define NDX_NA_9(a, b, ...)   args.b, NDX_NA_8(__VA_ARGS__)
#define NDX_NA_10(a, b, ...)  args.b, NDX_NA_9(__VA_ARGS__)
#define NDX_NA_11(a, b, ...)  args.b, NDX_NA_10(__VA_ARGS__)
#define NDX_NA_12(a, b, ...)  args.b, NDX_NA_11(__VA_ARGS__)
#define NDX_NA_13(a, b, ...)  args.b, NDX_NA_12(__VA_ARGS__)
#define NDX_NA_14(a, b, ...)  args.b, NDX_NA_13(__VA_ARGS__)
#define NDX_NA_15(a, b, ...)  args.b, NDX_NA_14(__VA_ARGS__)
#define NDX_NA_16(a, b, ...)  args.b, NDX_NA_15(__VA_ARGS__)

#define NDX_DA(...) CAT(NDX_DA_, \
		NDX_PC(__VA_ARGS__))( __VA_ARGS__)

#define NDX_DA_1(a, b)        b
#define NDX_DA_2(a, b, ...)   b, NDX_DA_1(__VA_ARGS__)
#define NDX_DA_3(a, b, ...)   b, NDX_DA_2(__VA_ARGS__)
#define NDX_DA_4(a, b, ...)   b, NDX_DA_3(__VA_ARGS__)
#define NDX_DA_5(a, b, ...)   b, NDX_DA_4(__VA_ARGS__)
#define NDX_DA_6(a, b, ...)   b, NDX_DA_5(__VA_ARGS__)
#define NDX_DA_7(a, b, ...)   b, NDX_DA_6(__VA_ARGS__)
#define NDX_DA_8(a, b, ...)   b, NDX_DA_7(__VA_ARGS__)
#define NDX_DA_9(a, b, ...)   b, NDX_DA_8(__VA_ARGS__)
#define NDX_DA_10(a, b, ...)  b, NDX_DA_9(__VA_ARGS__)
#define NDX_DA_11(a, b, ...)  b, NDX_DA_10(__VA_ARGS__)
#define NDX_DA_12(a, b, ...)  b, NDX_DA_11(__VA_ARGS__)
#define NDX_DA_13(a, b, ...)  b, NDX_DA_12(__VA_ARGS__)
#define NDX_DA_14(a, b, ...)  b, NDX_DA_13(__VA_ARGS__)
#define NDX_DA_15(a, b, ...)  b, NDX_DA_14(__VA_ARGS__)
#define NDX_DA_16(a, b, ...)  b, NDX_DA_15(__VA_ARGS__)

#define STR(x) #x
#define XSTR(x) STR(x)

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

/**
 * @brief Declare a mod-callable function signature and call stubs.
 *
 * Use this in common header files shared between modules, or in modules
 * that only need to call hooks without implementing them.
 *
 * Creates:
 * - \c fname##_t: Function typedef for the hook
 * - \c fname: Weak function symbol (optional implementation)
 * - \c struct fname##_args: Argument structure
 * - \c fname##_id: Adapter ID (set at runtime)
 * - \c call_##fname(): Typed call wrapper function
 *
 * @param ftype Return type (e.g., int, void)
 * @param fname Function name (e.g., on_tick)
 * @param ... Argument types and names (e.g., int, dt)
 *
 * Example:
 * @code
 * NDX_DECL(int, on_tick, int, dt);
 * // Creates: on_tick_t, struct on_tick_args, on_tick_id, call_on_tick(int)
 * @endcode
 */
/* the callee uses this to be called */
#define NDX_DECL(ftype, fname, ...) \
	typedef ftype fname##_t(NDX_FA(__VA_ARGS__)); \
	fname##_t fname WEAK; \
	struct fname##_args { \
		NDX_PG(__VA_ARGS__) \
	}; \
	static inline UNUSED \
	ftype call_##fname(NDX_FA(__VA_ARGS__)) { \
		ftype ret; \
		memset(&ret, 0, sizeof(ret)); \
		NDX_CALL(&ret, fname, NDX_DA(__VA_ARGS__)); \
		return ret; \
	}

/**
 * @brief Define a mod-callable function (hook).
 *
 * Use this in modules that implement hooks. Allows the host to call
 * this function from loaded modules.
 *
 * Creates:
 * - \c fname##_t: Function typedef
 * - \c fname: Function implementation (you provide this)
 * - \c fname##_id: Hook ID (set on registration)
 * - \c call_##fname(): Typed call wrapper
 *
 * The hook is auto-registered when the module loads.
 *
 * @param ftype Return type (e.g., int, void)
 * @param fname Function name (e.g., on_tick)
 * @param ... Argument types and names (e.g., int, dt)
 *
 * Example:
 * @code
 * NDX_DEF(int, on_tick, int, dt);
 * int on_tick(int dt) { return dt + 1; }
 * @endcode
 */
#define NDX_DEF(ftype, fname, ...) \
	typedef ftype fname##_t(NDX_FA(__VA_ARGS__)); \
	fname##_t fname; \
	struct fname##_args { \
		NDX_PG(__VA_ARGS__) \
	}; \
	static inline UNUSED \
	ftype call_##fname(NDX_FA(__VA_ARGS__)) { \
		ftype ret; \
		memset(&ret, 0, sizeof(ret)); \
		NDX_CALL(&ret, fname, NDX_DA(__VA_ARGS__)); \
		return ret; \
	} \
	_Static_assert(sizeof(ftype) <= NDX_MAX_RET_SIZE, \
		"return type too large for ndx_adapter_t"); \
	fname##_t fname; \
	void fname##_adapter_call(void *res, void *fn, void *arg) { \
	fname##_t *cast_fn; \
		struct fname##_args args; \
	memcpy(&args, arg, sizeof(args)); \
	if (!fn) { \
		WARN("%s_adapter_call: '%s' wasn't defined\n", \
				XSTR(fname), XSTR(fname)); \
		return; \
	} \
	* (void **) &cast_fn = fn; \
		ftype result = cast_fn(NDX_NA(__VA_ARGS__)); \
	if (res) memcpy(res, &result, sizeof(ftype)); \
	} \
	ndx_adapter_t fname##_adapter  __attribute__((visibility("default"))) = { \
		.name = XSTR(fname), \
		.arg_size = sizeof(struct fname##_args), \
		.ret_size = sizeof(ftype), \
		.call = &fname##_adapter_call, \
	}; \
	void fname##_adapter_reg(void) { \
		ndx_areg(XSTR(fname), &fname##_adapter); \
	} \
	AUTO_INIT \
	void (*fname##_adapter_reg_p)(void) = fname##_adapter_reg; \
	ftype fname(NDX_FA(__VA_ARGS__))

/**
 * @brief Call a mod hook by name, using its hook id.
 *
 * This macro calls all modules that implement the named hook.
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
	ndx.call(retp, XSTR(fname), &args); \
}

/* Internal types - used by ndx_t struct */
typedef unsigned ndx_areg_t(char *name, ndx_adapter_t *adapter);
typedef int ndx_call_t(void *retp, char *name, void *args);
typedef int ndx_last_t(void *ret);

ndx_areg_t ndx_areg;
ndx_call_t ndx_call;
ndx_last_t ndx_last;

/**
 * @brief Load or reload a module.
 *
 * @param fname Path to .so (Linux) or .dll (Windows) file
 * @return NDX_OK on success, negative error code on failure
 *
 * On first load, calls ndx_install()
 * it doesn't if it is already loaded.
 */
typedef int ndx_load_t(char *fname);
ndx_load_t ndx_load;

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

struct ndx_ctx {
	ndx_call_t *call;
	ndx_areg_t *areg;
	ndx_load_t *load;
	ndx_errno_t *err;
	ndx_strerror_t *strerror;
	ndx_adapter_t *adapter;
	ndx_last_t *last;
	ndx_shutdown_t *shutdown;
};

#endif

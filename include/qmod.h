#ifndef QMOD_H
#define QMOD_H

/* RECOMMENDATIONS:
 *
 * - Avoid passing entire objects in QMOD calls. It's not very
 *   useful since getting / putting things by id can be fast.
 *   Only when you really don't have another way because you
 *   modify the object in the calling function and don't put
 *   and set around the QMOD_CALL. Usually mods will use their
 *   custom object types, anyway.
 */

#include <stddef.h>

typedef struct {
	char name[64];
	size_t arg_size;
	size_t ret_size;
	void (*call)(void *, void *, void *);
	char ret[5096];
	unsigned ran;
} qmod_adapter_t;

#define CAT(a, ...) PRIMITIVE_CAT(a, __VA_ARGS__)
#define PRIMITIVE_CAT(a, ...) a ## __VA_ARGS__

#define QMOD_PC(...) \
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

#define QMOD_FA(...) CAT(QMOD_FA_, \
		QMOD_PC(__VA_ARGS__))( __VA_ARGS__)

#define QMOD_FA_1(a, b)        a b
#define QMOD_FA_2(a, b, ...)   a b, QMOD_FA_1(__VA_ARGS__)
#define QMOD_FA_3(a, b, ...)   a b, QMOD_FA_2(__VA_ARGS__)
#define QMOD_FA_4(a, b, ...)   a b, QMOD_FA_3(__VA_ARGS__)
#define QMOD_FA_5(a, b, ...)   a b, QMOD_FA_4(__VA_ARGS__)
#define QMOD_FA_6(a, b, ...)   a b, QMOD_FA_5(__VA_ARGS__)
#define QMOD_FA_7(a, b, ...)   a b, QMOD_FA_6(__VA_ARGS__)
#define QMOD_FA_8(a, b, ...)   a b, QMOD_FA_7(__VA_ARGS__)
#define QMOD_FA_9(a, b, ...)   a b, QMOD_FA_8(__VA_ARGS__)
#define QMOD_FA_10(a, b, ...)  a b, QMOD_FA_9(__VA_ARGS__)
#define QMOD_FA_11(a, b, ...)  a b, QMOD_FA_10(__VA_ARGS__)
#define QMOD_FA_12(a, b, ...)  a b, QMOD_FA_11(__VA_ARGS__)
#define QMOD_FA_13(a, b, ...)  a b, QMOD_FA_12(__VA_ARGS__)
#define QMOD_FA_14(a, b, ...)  a b, QMOD_FA_13(__VA_ARGS__)
#define QMOD_FA_15(a, b, ...)  a b, QMOD_FA_14(__VA_ARGS__)
#define QMOD_FA_16(a, b, ...)  a b, QMOD_FA_15(__VA_ARGS__)

#define QMOD_PG(...) CAT(QMOD_PG_, \
		QMOD_PC(__VA_ARGS__))( __VA_ARGS__)

#define QMOD_PG_1(a, b)        a b;
#define QMOD_PG_2(a, b, ...)   a b; QMOD_PG_1(__VA_ARGS__)
#define QMOD_PG_3(a, b, ...)   a b; QMOD_PG_2(__VA_ARGS__)
#define QMOD_PG_4(a, b, ...)   a b; QMOD_PG_3(__VA_ARGS__)
#define QMOD_PG_5(a, b, ...)   a b; QMOD_PG_4(__VA_ARGS__)
#define QMOD_PG_6(a, b, ...)   a b; QMOD_PG_5(__VA_ARGS__)
#define QMOD_PG_7(a, b, ...)   a b; QMOD_PG_6(__VA_ARGS__)
#define QMOD_PG_8(a, b, ...)   a b; QMOD_PG_7(__VA_ARGS__)
#define QMOD_PG_9(a, b, ...)   a b; QMOD_PG_8(__VA_ARGS__)
#define QMOD_PG_10(a, b, ...)  a b; QMOD_PG_9(__VA_ARGS__)
#define QMOD_PG_11(a, b, ...)  a b; QMOD_PG_10(__VA_ARGS__)
#define QMOD_PG_12(a, b, ...)  a b; QMOD_PG_11(__VA_ARGS__)
#define QMOD_PG_13(a, b, ...)  a b; QMOD_PG_12(__VA_ARGS__)
#define QMOD_PG_14(a, b, ...)  a b; QMOD_PG_13(__VA_ARGS__)
#define QMOD_PG_15(a, b, ...)  a b; QMOD_PG_14(__VA_ARGS__)
#define QMOD_PG_16(a, b, ...)  a b; QMOD_PG_15(__VA_ARGS__)

#define QMOD_NA(...) CAT(QMOD_NA_, \
		QMOD_PC(__VA_ARGS__))( __VA_ARGS__)

#define QMOD_NA_1(a, b)        args.b
#define QMOD_NA_2(a, b, ...)   args.b, QMOD_NA_1(__VA_ARGS__)
#define QMOD_NA_3(a, b, ...)   args.b, QMOD_NA_2(__VA_ARGS__)
#define QMOD_NA_4(a, b, ...)   args.b, QMOD_NA_3(__VA_ARGS__)
#define QMOD_NA_5(a, b, ...)   args.b, QMOD_NA_4(__VA_ARGS__)
#define QMOD_NA_6(a, b, ...)   args.b, QMOD_NA_5(__VA_ARGS__)
#define QMOD_NA_7(a, b, ...)   args.b, QMOD_NA_6(__VA_ARGS__)
#define QMOD_NA_8(a, b, ...)   args.b, QMOD_NA_7(__VA_ARGS__)
#define QMOD_NA_9(a, b, ...)   args.b, QMOD_NA_8(__VA_ARGS__)
#define QMOD_NA_10(a, b, ...)  args.b, QMOD_NA_9(__VA_ARGS__)
#define QMOD_NA_11(a, b, ...)  args.b, QMOD_NA_10(__VA_ARGS__)
#define QMOD_NA_12(a, b, ...)  args.b, QMOD_NA_11(__VA_ARGS__)
#define QMOD_NA_13(a, b, ...)  args.b, QMOD_NA_12(__VA_ARGS__)
#define QMOD_NA_14(a, b, ...)  args.b, QMOD_NA_13(__VA_ARGS__)
#define QMOD_NA_15(a, b, ...)  args.b, QMOD_NA_14(__VA_ARGS__)
#define QMOD_NA_16(a, b, ...)  args.b, QMOD_NA_15(__VA_ARGS__)

#define QMOD_DA(...) CAT(QMOD_DA_, \
		QMOD_PC(__VA_ARGS__))( __VA_ARGS__)

#define QMOD_DA_1(a, b)        b
#define QMOD_DA_2(a, b, ...)   b, QMOD_DA_1(__VA_ARGS__)
#define QMOD_DA_3(a, b, ...)   b, QMOD_DA_2(__VA_ARGS__)
#define QMOD_DA_4(a, b, ...)   b, QMOD_DA_3(__VA_ARGS__)
#define QMOD_DA_5(a, b, ...)   b, QMOD_DA_4(__VA_ARGS__)
#define QMOD_DA_6(a, b, ...)   b, QMOD_DA_5(__VA_ARGS__)
#define QMOD_DA_7(a, b, ...)   b, QMOD_DA_6(__VA_ARGS__)
#define QMOD_DA_8(a, b, ...)   b, QMOD_DA_7(__VA_ARGS__)
#define QMOD_DA_9(a, b, ...)   b, QMOD_DA_8(__VA_ARGS__)
#define QMOD_DA_10(a, b, ...)  b, QMOD_DA_9(__VA_ARGS__)
#define QMOD_DA_11(a, b, ...)  b, QMOD_DA_10(__VA_ARGS__)
#define QMOD_DA_12(a, b, ...)  b, QMOD_DA_11(__VA_ARGS__)
#define QMOD_DA_13(a, b, ...)  b, QMOD_DA_12(__VA_ARGS__)
#define QMOD_DA_14(a, b, ...)  b, QMOD_DA_13(__VA_ARGS__)
#define QMOD_DA_15(a, b, ...)  b, QMOD_DA_14(__VA_ARGS__)
#define QMOD_DA_16(a, b, ...)  b, QMOD_DA_15(__VA_ARGS__)

#define STR(x) #x
#define XSTR(x) STR(x)

typedef void (*mod_cb_t)(void);

#ifndef __ID_MARKER__
#define __ID_MARKER__
#endif

/* the callee uses this to be called */
#define QMOD_DECL(ftype, fname, ...) \
    typedef ftype fname##_t(QMOD_FA(__VA_ARGS__)); \
    fname##_t fname __attribute__((weak)); \
    struct fname##_args { \
        QMOD_PG(__VA_ARGS__) \
    }; \
    __ID_MARKER__ unsigned fname##_id; \
    static inline __attribute__((unused)) \
    ftype call_##fname(QMOD_FA(__VA_ARGS__)) { \
	    ftype ret; \
	    memset(&ret, 0, sizeof(ret)); \
	    QMOD_CALL(&ret, fname, QMOD_DA(__VA_ARGS__)); \
	    return ret; \
    } \
    __attribute__((used)) \
    static void fname##_init_id(void) { \
	    fname##_id = qmod_get(XSTR(fname)); \
    } \
    __attribute__((used, section(".qmod_auto_init"))) \
    static mod_cb_t fname##_init_id_p = &fname##_init_id // note lack of semicolon

#define QMOD_DEF(ftype, fname, ...) \
    fname##_t fname; \
    void fname##_adapter_call(void *res, void *fname, void *arg) { \
        struct fname##_args args; \
	memcpy(&args, arg, sizeof(args)); \
	if (!fname) { \
		WARN("%s_adapter_call: '%s' wasn't defined\n", \
				XSTR(fname), XSTR(fname)); \
		return; \
	} \
        ftype result = ((fname##_t *) fname)(QMOD_NA(__VA_ARGS__)); \
	if (res) memcpy(res, &result, sizeof(ftype)); \
    } \
    qmod_adapter_t fname##_adapter  __attribute__((visibility("default"))) = { \
	    .name = XSTR(fname), \
	    .arg_size = sizeof(struct fname##_args), \
	    .ret_size = sizeof(ftype), \
	    .call = &fname##_adapter_call, \
    }; \
    void fname##_adapter_reg(void) { \
	    fname##_id = qmod_areg(XSTR(fname), &fname##_adapter); \
    } \
    __attribute__((used, section(".qmod_auto_init"))) \
    void (*fname##_adapter_reg_p)(void) = fname##_adapter_reg // note lack of semicolon

/* the caller uses this to call */
#define QMOD_CALL(retp, fname, ...) { \
	struct fname##_args args = { __VA_ARGS__ }; \
	if (fname##_id == NOTHING) \
		WARN("QMOD_CALL BAD %s\n", XSTR(fname)); \
	qmod_call(retp, fname##_id, &args); \
}

typedef unsigned qmod_areg_t(char *name,
		qmod_adapter_t *adapter);
qmod_areg_t qmod_areg;

typedef void qmod_call_t(void *retp,
		unsigned id, void *args);
qmod_call_t qmod_call;

typedef int qmod_last_t(void *ret);
qmod_last_t qmod_last;

typedef unsigned qmod_get_t(char *name);
qmod_get_t qmod_get;

extern unsigned type_hd;

#endif

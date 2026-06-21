#ifndef XY_PP_H
#define XY_PP_H

#define CAT(a, ...) PRIMITIVE_CAT(a, __VA_ARGS__)
#define PRIMITIVE_CAT(a, ...) a ## __VA_ARGS__

#define XY_PC(...) \
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

#define XY_FA(...) CAT(XY_FA_, \
		XY_PC(__VA_ARGS__))( __VA_ARGS__)

#define XY_FA_1(a, b)        a b
#define XY_FA_2(a, b, ...)   a b, XY_FA_1(__VA_ARGS__)
#define XY_FA_3(a, b, ...)   a b, XY_FA_2(__VA_ARGS__)
#define XY_FA_4(a, b, ...)   a b, XY_FA_3(__VA_ARGS__)
#define XY_FA_5(a, b, ...)   a b, XY_FA_4(__VA_ARGS__)
#define XY_FA_6(a, b, ...)   a b, XY_FA_5(__VA_ARGS__)
#define XY_FA_7(a, b, ...)   a b, XY_FA_6(__VA_ARGS__)
#define XY_FA_8(a, b, ...)   a b, XY_FA_7(__VA_ARGS__)
#define XY_FA_9(a, b, ...)   a b, XY_FA_8(__VA_ARGS__)
#define XY_FA_10(a, b, ...)  a b, XY_FA_9(__VA_ARGS__)
#define XY_FA_11(a, b, ...)  a b, XY_FA_10(__VA_ARGS__)
#define XY_FA_12(a, b, ...)  a b, XY_FA_11(__VA_ARGS__)
#define XY_FA_13(a, b, ...)  a b, XY_FA_12(__VA_ARGS__)
#define XY_FA_14(a, b, ...)  a b, XY_FA_13(__VA_ARGS__)
#define XY_FA_15(a, b, ...)  a b, XY_FA_14(__VA_ARGS__)
#define XY_FA_16(a, b, ...)  a b, XY_FA_15(__VA_ARGS__)

#define XY_PG(...) CAT(XY_PG_, \
		XY_PC(__VA_ARGS__))( __VA_ARGS__)

#define XY_PG_1(a, b)        a b;
#define XY_PG_2(a, b, ...)   a b; XY_PG_1(__VA_ARGS__)
#define XY_PG_3(a, b, ...)   a b; XY_PG_2(__VA_ARGS__)
#define XY_PG_4(a, b, ...)   a b; XY_PG_3(__VA_ARGS__)
#define XY_PG_5(a, b, ...)   a b; XY_PG_4(__VA_ARGS__)
#define XY_PG_6(a, b, ...)   a b; XY_PG_5(__VA_ARGS__)
#define XY_PG_7(a, b, ...)   a b; XY_PG_6(__VA_ARGS__)
#define XY_PG_8(a, b, ...)   a b; XY_PG_7(__VA_ARGS__)
#define XY_PG_9(a, b, ...)   a b; XY_PG_8(__VA_ARGS__)
#define XY_PG_10(a, b, ...)  a b; XY_PG_9(__VA_ARGS__)
#define XY_PG_11(a, b, ...)  a b; XY_PG_10(__VA_ARGS__)
#define XY_PG_12(a, b, ...)  a b; XY_PG_11(__VA_ARGS__)
#define XY_PG_13(a, b, ...)  a b; XY_PG_12(__VA_ARGS__)
#define XY_PG_14(a, b, ...)  a b; XY_PG_13(__VA_ARGS__)
#define XY_PG_15(a, b, ...)  a b; XY_PG_14(__VA_ARGS__)
#define XY_PG_16(a, b, ...)  a b; XY_PG_15(__VA_ARGS__)

#define XY_NP(...) CAT(XY_NP_, \
		XY_PC(__VA_ARGS__))( __VA_ARGS__)

#define XY_NP_1(a, b)        __xy_a->b
#define XY_NP_2(a, b, ...)   __xy_a->b, XY_NP_1(__VA_ARGS__)
#define XY_NP_3(a, b, ...)   __xy_a->b, XY_NP_2(__VA_ARGS__)
#define XY_NP_4(a, b, ...)   __xy_a->b, XY_NP_3(__VA_ARGS__)
#define XY_NP_5(a, b, ...)   __xy_a->b, XY_NP_4(__VA_ARGS__)
#define XY_NP_6(a, b, ...)   __xy_a->b, XY_NP_5(__VA_ARGS__)
#define XY_NP_7(a, b, ...)   __xy_a->b, XY_NP_6(__VA_ARGS__)
#define XY_NP_8(a, b, ...)   __xy_a->b, XY_NP_7(__VA_ARGS__)
#define XY_NP_9(a, b, ...)   __xy_a->b, XY_NP_8(__VA_ARGS__)
#define XY_NP_10(a, b, ...)  __xy_a->b, XY_NP_9(__VA_ARGS__)
#define XY_NP_11(a, b, ...)  __xy_a->b, XY_NP_10(__VA_ARGS__)
#define XY_NP_12(a, b, ...)  __xy_a->b, XY_NP_11(__VA_ARGS__)
#define XY_NP_13(a, b, ...)  __xy_a->b, XY_NP_12(__VA_ARGS__)
#define XY_NP_14(a, b, ...)  __xy_a->b, XY_NP_13(__VA_ARGS__)
#define XY_NP_15(a, b, ...)  __xy_a->b, XY_NP_14(__VA_ARGS__)
#define XY_NP_16(a, b, ...)  __xy_a->b, XY_NP_15(__VA_ARGS__)

#define XY_DA(...) CAT(XY_DA_, \
		XY_PC(__VA_ARGS__))( __VA_ARGS__)

#define XY_DA_1(a, b)        b
#define XY_DA_2(a, b, ...)   b, XY_DA_1(__VA_ARGS__)
#define XY_DA_3(a, b, ...)   b, XY_DA_2(__VA_ARGS__)
#define XY_DA_4(a, b, ...)   b, XY_DA_3(__VA_ARGS__)
#define XY_DA_5(a, b, ...)   b, XY_DA_4(__VA_ARGS__)
#define XY_DA_6(a, b, ...)   b, XY_DA_5(__VA_ARGS__)
#define XY_DA_7(a, b, ...)   b, XY_DA_6(__VA_ARGS__)
#define XY_DA_8(a, b, ...)   b, XY_DA_7(__VA_ARGS__)
#define XY_DA_9(a, b, ...)   b, XY_DA_8(__VA_ARGS__)
#define XY_DA_10(a, b, ...)  b, XY_DA_9(__VA_ARGS__)
#define XY_DA_11(a, b, ...)  b, XY_DA_10(__VA_ARGS__)
#define XY_DA_12(a, b, ...)  b, XY_DA_11(__VA_ARGS__)
#define XY_DA_13(a, b, ...)  b, XY_DA_12(__VA_ARGS__)
#define XY_DA_14(a, b, ...)  b, XY_DA_13(__VA_ARGS__)
#define XY_DA_15(a, b, ...)  b, XY_DA_14(__VA_ARGS__)
#define XY_DA_16(a, b, ...)  b, XY_DA_15(__VA_ARGS__)

#define STR(x) #x
#define XSTR(x) STR(x)

#endif

#include <ttypt/ndx.h>
#include "../../src/papi.h"

ndx_t ndx;

MODULE_API int multi_arg(int a, int b, int c, int d, int e, int f, int g, int h) {
	return a + b + c + d + e + f + g + h;
}

typedef struct {
	int x;
	int y;
} point_t;

MODULE_API point_t struct_hook(int x, int y) {
	point_t p = {x, y};
	return p;
}

MODULE_API int void_arg_hook(void) {
	return 42;
}

MODULE_API void ndx_install(void) {}

MODULE_API ndx_t* get_ndx_ptr(void) {
	return &ndx;
}

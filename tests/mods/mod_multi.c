#include <ttypt/xy.h>
#include "../../src/papi.h"

xy_t xy;

XY_MODULE_API int multi_arg(int a, int b, int c, int d, int e, int f, int g, int h) {
	return a + b + c + d + e + f + g + h;
}

typedef struct {
	int x;
	int y;
} point_t;

XY_MODULE_API point_t struct_hook(int x, int y) {
	point_t p = {x, y};
	return p;
}

XY_MODULE_API int void_arg_hook(void) {
	return 42;
}

XY_MODULE_API void xy_install(void) {}

XY_MODULE_API xy_t* get_xy_ptr(void) {
	return &xy;
}

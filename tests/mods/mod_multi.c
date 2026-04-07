#include <ttypt/ndx-mod.h>

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

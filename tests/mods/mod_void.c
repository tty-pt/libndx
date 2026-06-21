#include <ttypt/xy.h>
#include "../../src/papi.h"

xy_t xy;

static int call_count = 0;

XY_MODULE_API void void_hook(int val) {
	call_count += val;
}

XY_MODULE_API int get_call_count(void) {
	return call_count;
}

XY_MODULE_API void xy_install(void) {
	call_count = 0;
}

XY_MODULE_API xy_t* get_xy_ptr(void) {
	return &xy;
}

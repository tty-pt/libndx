#include <ttypt/xy.h>
#include "../../src/papi.h"

xy_t xy;

XY_MODULE_API int on_tick(int dt) {
	return dt + 1;
}

XY_MODULE_API int thread_hook(int val) {
	return val * 2;
}

XY_MODULE_API void xy_install(void) {}

XY_MODULE_API xy_t* get_xy_ptr(void) {
	return &xy;
}

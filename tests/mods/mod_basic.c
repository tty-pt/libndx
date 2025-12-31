#include <ttypt/ndx.h>
#include "../../src/papi.h"

ndx_t ndx;

MODULE_API int on_tick(int dt) {
	return dt + 1;
}

MODULE_API int thread_hook(int val) {
	return val * 2;
}

MODULE_API void ndx_install(void) {}

MODULE_API ndx_t* get_ndx_ptr(void) {
	return &ndx;
}

#include <ttypt/ndx.h>
#include "../../src/papi.h"

ndx_t ndx;

static int call_count = 0;

MODULE_API void void_hook(int val) {
	call_count += val;
}

MODULE_API int get_call_count(void) {
	return call_count;
}

MODULE_API void ndx_install(void) {
	call_count = 0;
}

MODULE_API ndx_t* get_ndx_ptr(void) {
	return &ndx;
}

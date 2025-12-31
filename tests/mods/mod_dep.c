#include <ttypt/ndx.h>
#include "../../src/papi.h"

ndx_t ndx;

/* declare dependency on mod_basic.so */
MODULE_API const char *ndx_deps[] = { "./tests/mods/mod_basic.so", NULL };

MODULE_API int dep_hook(int x) {
	return x + 7;
}

MODULE_API void ndx_install(void) {}

MODULE_API ndx_t* get_ndx_ptr(void) {
	return &ndx;
}

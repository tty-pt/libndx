#include <ttypt/ndx.h>
#include "../../src/papi.h"

ndx_t ndx;

MODULE_API int bare_hook(int x) {
	return x + 100;
}

MODULE_API void ndx_install(void) {}


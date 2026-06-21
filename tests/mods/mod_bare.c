#include <ttypt/xy.h>
#include "../../src/papi.h"

xy_t xy;

XY_MODULE_API int bare_hook(int x) {
	return x + 100;
}

XY_MODULE_API void xy_install(void) {}


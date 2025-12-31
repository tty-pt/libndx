#include <ttypt/ndx.h>
#include "../../src/papi.h"

ndx_t ndx;

/* module C depends on module B, to form a cycle B->C->B */
MODULE_API const char *ndx_deps[] = { "./tests/mods/mod_dep_b.so", NULL };

MODULE_API void ndx_install(void) {}

MODULE_API ndx_t* get_ndx_ptr(void) {
	return &ndx;
}

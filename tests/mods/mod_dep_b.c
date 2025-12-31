#include <ttypt/ndx.h>
#include "../../src/papi.h"

ndx_t ndx;

/* module B depends on module A, to be used in cycle test */
MODULE_API const char *ndx_deps[] = { "./tests/mods/mod_dep_c.so", NULL };

MODULE_API void ndx_install(void) {}

MODULE_API ndx_t* get_ndx_ptr(void) {
	return &ndx;
}

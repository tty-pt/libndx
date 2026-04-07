#include <ttypt/ndx-mod.h>
#include "../../tests/test_hooks.h"
#include <stdio.h>

MODULE_API void ndx_install(void) {
    if (ndx_load("./tests/mods/mod_dep_provider") != NDX_OK) {
        fprintf(stderr, "failed to load provider: %s\n", ndx_strerror(ndx_errno()));
        return;
    }
}

#include <ttypt/ndx-mod.h>
#include "../../tests/test_hooks.h"
#include <stdio.h>

void ndx_install(void) {
    if (ndx_load("./tests/mods/mod_dep_provider") != NDX_OK) {
        fprintf(stderr, "failed to load provider: %s\n", ndx_strerror(ndx.err()));
        return;
    }
}

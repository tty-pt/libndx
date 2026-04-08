#include <ttypt/ndx.h>
#include "../src/papi.h"
#include "../../tests/test_hooks.h"
#include <stdio.h>

ndx_t ndx;

void ndx_install(void) {
    if (ndx_load("./tests/mods/mod_dep_provider") != NDX_OK) {
        fprintf(stderr, "failed to load provider: %s\n", ndx_strerror(ndx_errno()));
        return;
    }
}

ndx_t* get_ndx_ptr(void) {
    return &ndx;
}

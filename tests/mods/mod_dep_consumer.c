#include <ttypt/xy-mod.h>
#include "../../tests/test_hooks.h"
#include <stdio.h>

void xy_install(void) {
    if (xy_load("./tests/mods/mod_dep_provider") != XY_OK) {
        fprintf(stderr, "failed to load provider: %s\n", xy_strerror(xy.err()));
        return;
    }
}

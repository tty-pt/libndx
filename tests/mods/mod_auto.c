#include <ttypt/ndx.h>
#include "../../src/papi.h"
#include <stdio.h>

ndx_t ndx;

static int init_order = 0;
static int initRan = 0;
static int installRan = 0;

int get_init_order(void) { return init_order; }
int get_init_ran(void) { return initRan; }
int get_install_ran(void) { return installRan; }

void mod_auto_init(void) {
    init_order = 1;
    initRan = 1;
    printf("mod_auto_init called\n");
}

void ndx_install(void) {
    if (init_order == 1) {
        init_order = 2;
    }
    installRan = 1;
    printf("ndx_install called\n");
}

MODULE_API ndx_t* get_ndx_ptr(void) {
    return &ndx;
}

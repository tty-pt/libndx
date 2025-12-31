#include <ttypt/ndx.h>
#include "../../src/papi.h"

ndx_t ndx;

static int adder_value = 1;

MODULE_API int add_value(int x) {
    return x + adder_value;
}

MODULE_API void ndx_install(void) {}

MODULE_API ndx_t* get_ndx_ptr(void) {
    return &ndx;
}

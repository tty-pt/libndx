#include <ttypt/ndx.h>
#include "../../src/papi.h"

ndx_t ndx;

static int counter = 100;

MODULE_API int get_counter(void) {
    return counter;
}

MODULE_API void increment_counter(int amount) {
    counter += amount;
}

MODULE_API void ndx_install(void) {
    counter = 100;
}

MODULE_API ndx_t* get_ndx_ptr(void) {
    return &ndx;
}

#include <ttypt/xy.h>
#include "../../src/papi.h"

xy_t xy;

static int multiplier_value = 2;

XY_MODULE_API int add_value(int x) {
    return x * multiplier_value;
}

XY_MODULE_API void xy_install(void) {}

XY_MODULE_API xy_t* get_xy_ptr(void) {
    return &xy;
}

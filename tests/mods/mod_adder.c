#include <ttypt/ndx-mod.h>

static int adder_value = 1;

MODULE_API int add_value(int x) {
    return x + adder_value;
}

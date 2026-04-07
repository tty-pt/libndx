#include <ttypt/ndx-mod.h>

static int multiplier_value = 2;

MODULE_API int add_value(int x) {
    return x * multiplier_value;
}

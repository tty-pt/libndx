#include <ttypt/ndx-mod.h>

static int counter = 100;

MODULE_API int get_counter(void) {
    return counter;
}

MODULE_API void increment_counter(int amount) {
    counter += amount;
}

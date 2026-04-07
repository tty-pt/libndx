#include <ttypt/ndx-mod.h>

static int call_count = 0;

MODULE_API void void_hook(int val) {
	call_count += val;
}

MODULE_API int get_call_count(void) {
	return call_count;
}

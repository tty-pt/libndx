#include <ttypt/ndx-mod.h>

MODULE_API int on_tick(int dt) {
	return dt + 1;
}

MODULE_API int thread_hook(int val) {
	return val * 2;
}

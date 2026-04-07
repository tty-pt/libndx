#include <ttypt/ndx-mod.h>

static int on_tick_mode;

MODULE_API int
on_tick(int dt)
{
	return dt + on_tick_mode;
}

MODULE_API void
ndx_install(void)
{
	on_tick_mode = 1;
}

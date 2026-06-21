#include <ttypt/xy.h>

#include "../src/papi.h"

xy_t xy;

static int on_tick_mode;

XY_MODULE_API int
on_tick(int dt)
{
	return dt + on_tick_mode;
}

XY_MODULE_API void
xy_install(void)
{
	on_tick_mode = 1;
}

XY_MODULE_API xy_t *
get_xy_ptr(void)
{
	return &xy;
}

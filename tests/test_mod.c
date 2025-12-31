#include <ttypt/ndx.h>

#include "../src/papi.h"

ndx_t ndx;

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

MODULE_API ndx_t *
get_ndx_ptr(void)
{
	return &ndx;
}

#include <ttypt/xy-mod.h>

static int counter = 100;

XY_LISTENER(int, get_counter, int, dummy)
{
	(void)dummy;
	return counter;
}

XY_LISTENER(int, increment_counter, int, amount)
{
	counter += amount;
	return 0;
}

XY_MODULE_API void xy_install(void)
{
	counter = 100;
}

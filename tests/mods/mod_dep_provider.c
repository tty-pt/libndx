#include <ttypt/ndx-mod.h>

static int counter = 100;

NDX_DEF(int, get_counter, int, dummy)
{
	(void)dummy;
	return counter;
}

NDX_DEF(int, increment_counter, int, amount)
{
	counter += amount;
	return 0;
}

MODULE_API void ndx_install(void)
{
	counter = 100;
}

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <ttypt/ndx.h>

NDX_HOOK_DEF(int, on_tick, int, dt);

static void
build_mod_path(char *buf, size_t buf_len)
{
	snprintf(buf, buf_len, "./tests/test_mod");
}

int
main(void)
{
	char mod_path[128];
	int ret;
	int last;

	on_tick_adapter_reg();
	assert(on_tick_adapter.name[0] != '\0');

	ret = on_tick(10);
	assert(ret == 0);

	build_mod_path(mod_path, sizeof(mod_path));
	ndx_load(mod_path);

	ret = on_tick(10);
	assert(ret == 11);

	last = 0;
	assert(ndx_last(&last) == 0);
	assert(last == 11);

	ndx_load(mod_path);
	ret = on_tick(10);
	assert(ret == 11); /* second load is a no-op; still one module */

	last = 0;
	assert(ndx_last(&last) == 0);
	assert(last == 11);

	puts("ok");
	return 0;
}

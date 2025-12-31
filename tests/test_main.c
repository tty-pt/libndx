#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <ttypt/ndx.h>

typedef int on_tick_t(int dt);

struct on_tick_args {
	int dt;
};

unsigned on_tick_id = NDX_INVALID;

static void
on_tick_init_id(void)
{
	on_tick_id = ndx_get("on_tick");
}

static inline int
call_on_tick(int dt)
{
	int ret;

	memset(&ret, 0, sizeof(ret));
	NDX_CALL(&ret, on_tick, dt);
	return ret;
}

NDX_DEF(int, on_tick, int, dt);

static void
register_ndx(void)
{
	on_tick_adapter_reg();
}

static void
build_mod_path(char *buf, size_t buf_len)
{
#ifdef _WIN32
	const char *ext = "dll";
#else
	const char *ext = "so";
#endif
	snprintf(buf, buf_len, "./tests/test_mod.%s", ext);
}

int
main(void)
{
	char mod_path[128];
	int ret;
	int last;

	register_ndx();
	assert(on_tick_id != NDX_INVALID);

	ret = call_on_tick(10);
	assert(ret == 0);

	build_mod_path(mod_path, sizeof(mod_path));
	ndx_load(mod_path);

	ret = call_on_tick(10);
	assert(ret == 11);

	last = 0;
	assert(ndx_last(&last) == 0);
	assert(last == 11);

	ndx_load(mod_path);
	ret = call_on_tick(10);
	assert(ret == 12);

	last = 0;
	assert(ndx_last(&last) == 0);
	assert(last == 12);

	puts("ok");
	return 0;
}

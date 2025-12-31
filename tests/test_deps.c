#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <ttypt/ndx.h>

NDX_DECL(int, dep_hook, int, x);
NDX_DEF(int, dep_hook, int, x);

static void test_dep_loads_basic(void) {
	char path[128];
#ifdef _WIN32
	const char *ext = "dll";
#else
	const char *ext = "so";
#endif
	snprintf(path, sizeof(path), "./tests/mods/mod_dep.%s", ext);
	/* register adapter for dep_hook before loading modules */
	dep_hook_adapter_reg();
	/* ensure adapter registration succeeded */
	unsigned aid = ndx_get("dep_hook");
	fprintf(stderr, "debug pre-load: dep_hook_id=%u ndx_get(dep_hook)=%u\n", (unsigned)dep_hook_id, aid);
	fflush(stderr);

	int ret = ndx_load(path);
	fprintf(stderr, "debug post-load: dep_hook_id=%u ndx_get(dep_hook)=%u\n", (unsigned)dep_hook_id, ndx_get("dep_hook"));
	fflush(stderr);
	assert(ret == NDX_OK);

	/* debug: verify id variables */
	unsigned after_get = ndx_get("dep_hook");
	printf("debug: dep_hook_id(symbol) = %u, ndx_get('dep_hook') = %u\n", dep_hook_id, after_get);

	/* adapter for dep_hook should be registered by mod_dep */
	int r = call_dep_hook(3);
	/* mod_dep provides dep_hook(x) -> x+7 */
	assert(r == 10);

	printf("  test_dep_loads_basic: PASS\n");
}

static void test_missing_dep(void) {
	/* make a fake module that depends on a nonexistent file */
	/* we can't easily create a shared object here; rely on ndx_load_deps error */
	int ret = ndx_load_deps("./tests/mods/nonexistent_dep.so");
	assert(ret != NDX_OK);
	printf("  test_missing_dep: PASS\n");
}

static void test_cycle_detect(void) {
	char path[128];
#ifdef _WIN32
	const char *ext = "dll";
#else
	const char *ext = "so";
#endif
	snprintf(path, sizeof(path), "./tests/mods/mod_dep_b.%s", ext);
	int ret = ndx_load(path);
	assert(ret == NDX_ERR_CYCLE);
	printf("  test_cycle_detect: PASS\n");
}

int main(void) {
	printf("test_deps:\n");
	test_dep_loads_basic();
	test_missing_dep();
	test_cycle_detect();
	printf("  all tests passed\n");
	return 0;
}

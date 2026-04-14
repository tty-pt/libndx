#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <ttypt/ndx.h>

NDX_DEF(int, dummy_hook, int, x);

static void test_load_missing(void) {
	int ret = ndx_load("./tests/nonexistent_mod.so");
	assert(ret == NDX_ERR_NOTFOUND);
	assert(ndx_errno() == NDX_ERR_NOTFOUND);
	printf("  test_load_missing: PASS\n");
}

static void test_call_invalid_id(void) {
	int result = 0;
	struct dummy_hook_args args = { .x = 1 };
	int ret = ndx_call(&result, NULL, &args, NULL);
	assert(ret == NDX_ERR_NOTFOUND);
	assert(ndx_errno() == NDX_ERR_NOTFOUND);
	printf("  test_call_invalid_id: PASS\n");
}

static void test_last_no_call(void) {
	int result;
	int ret = ndx_last(&result);
	assert(ret == NDX_ERR_INVALID || ret == NDX_ERR_NOTFOUND);
	printf("  test_last_no_call: PASS\n");
}

static void test_errno_persists(void) {
	ndx_load("./tests/nonexistent.so");
	assert(ndx_errno() == NDX_ERR_NOTFOUND);
	assert(dummy_hook_adapter.name[0] != '\0');
	int result = call_dummy_hook(42);
	(void)result;
	assert(ndx_errno() == NDX_OK);
	printf("  test_errno_persists: PASS\n");
}

int main(void) {
	printf("test_errors:\n");
	
	test_load_missing();
	test_call_invalid_id();
	test_last_no_call();
	test_errno_persists();
	
	printf("  all tests passed\n");
	return 0;
}

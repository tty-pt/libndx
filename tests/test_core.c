#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <ttypt/ndx.h>

NDX_DEF(int, test_hook, int, a, int, b);

static void test_adapter_reg_sets_id(void) {
	assert(test_hook_adapter.name[0] != '\0');
	printf("  test_adapter_reg_sets_id: PASS\n");
}

static void test_call_no_mods(void) {
	int result = call_test_hook(5, 3);
	assert(result == 0);
	printf("  test_call_no_mods: PASS\n");
}

static void test_strerror(void) {
	assert(strcmp(ndx_strerror(NDX_OK), "success") == 0);
	assert(strcmp(ndx_strerror(NDX_ERR_NOTFOUND), "not found") == 0);
	assert(strcmp(ndx_strerror(NDX_ERR_INVALID), "invalid argument") == 0);
	assert(strcmp(ndx_strerror(NDX_ERR_TOOBIG), "return type too large") == 0);
	assert(strcmp(ndx_strerror(NDX_ERR_INIT), "initialization failed") == 0);
	assert(strcmp(ndx_strerror(-999), "unknown error") == 0);
	printf("  test_strerror: PASS\n");
}

static void test_errno_after_call(void) {
	int result;
	NDX_CALL(&result, test_hook, 1, 2);
	assert(ndx_errno() == NDX_OK);
	printf("  test_errno_after_call: PASS\n");
}

static void test_adapter_sizes(void) {
	assert(test_hook_adapter.arg_size == sizeof(struct test_hook_args));
	assert(test_hook_adapter.ret_size == sizeof(int));
	assert(strcmp(test_hook_adapter.name, "test_hook") == 0);
	printf("  test_adapter_sizes: PASS\n");
}

int main(void) {
	printf("test_core:\n");
	
	test_adapter_reg_sets_id();
	test_call_no_mods();
	test_strerror();
	test_errno_after_call();
	test_adapter_sizes();
	
	printf("  all tests passed\n");
	return 0;
}

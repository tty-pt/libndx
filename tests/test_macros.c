#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <ttypt/ndx.h>

static char mod_path[256];

NDX_DECL(int, multi_arg, int, a, int, b, int, c, int, d, int, e, int, f, int, g, int, h);
NDX_DEF(int, multi_arg, int, a, int, b, int, c, int, d, int, e, int, f, int, g, int, h);

static void test_multi_args(void) {
	int ret = ndx_load(mod_path);
	assert(ret == NDX_OK);
	
	int result = call_multi_arg(1, 2, 3, 4, 5, 6, 7, 8);
	assert(result == 36);
	printf("  test_multi_args: PASS\n");
}

typedef struct {
	int x;
	int y;
} point_t;

NDX_DECL(point_t, struct_hook, int, x, int, y);
NDX_DEF(point_t, struct_hook, int, x, int, y);

static void test_struct_return(void) {
	int ret = ndx_load(mod_path);
	assert(ret == NDX_OK);
	
	point_t p = call_struct_hook(10, 20);
	assert(p.x == 10);
	assert(p.y == 20);
	printf("  test_struct_return: PASS\n");
}

NDX_DECL(int, one_arg, int, a);
NDX_DEF(int, one_arg, int, a);
NDX_DECL(int, two_args, int, a, int, b);
NDX_DEF(int, two_args, int, a, int, b);
NDX_DECL(int, four_args, int, a, int, b, int, c, int, d);
NDX_DEF(int, four_args, int, a, int, b, int, c, int, d);
NDX_DECL(int, eight_args, int, a, int, b, int, c, int, d, int, e, int, f, int, g, int, h);
NDX_DEF(int, eight_args, int, a, int, b, int, c, int, d, int, e, int, f, int, g, int, h);

static void test_arg_counts(void) {
	assert(one_arg_adapter.arg_size == sizeof(struct one_arg_args));
	assert(two_args_adapter.arg_size == sizeof(struct two_args_args));
	assert(four_args_adapter.arg_size == sizeof(struct four_args_args));
	assert(eight_args_adapter.arg_size == sizeof(struct eight_args_args));
	printf("  test_arg_counts: PASS\n");
}

static void test_adapter_name(void) {
	assert(strcmp(one_arg_adapter.name, "one_arg") == 0);
	assert(strcmp(two_args_adapter.name, "two_args") == 0);
	assert(strcmp(multi_arg_adapter.name, "multi_arg") == 0);
	printf("  test_adapter_name: PASS\n");
}

static void build_mod_path(void) {
#ifdef _WIN32
	snprintf(mod_path, sizeof(mod_path), "./tests/mods/mod_multi.dll");
#else
	snprintf(mod_path, sizeof(mod_path), "./tests/mods/mod_multi.so");
#endif
}

int main(void) {
	printf("test_macros:\n");
	
	build_mod_path();
	
	test_multi_args();
	test_struct_return();
	test_arg_counts();
	test_adapter_name();
	
	printf("  all tests passed\n");
	return 0;
}
